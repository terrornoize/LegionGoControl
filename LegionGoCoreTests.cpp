#include "LegionGoCore.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;
int checks = 0;

void Check(bool condition, const char* expression, int line) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
    }
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

using namespace LegionGoCore;

GameProfile Profile(const wchar_t* name, const wchar_t* path,
                    TdpTriple tdp = {15, 20, 18}) {
    return GameProfile{name, path, tdp};
}

void TestTdpValidation() {
    std::wstring error = L"stale";
    CHECK(ValidateTdpTriple({5, 5, 5}, &error));
    CHECK(error.empty());
    CHECK(ValidateTdpTriple({35, 35, 35}));
    CHECK(ValidateTdpTriple({20, 35, 20}));

    CHECK(!ValidateTdpTriple({4, 5, 5}, &error));
    CHECK(error.find(L"STAPM") != std::wstring::npos);
    CHECK(!ValidateTdpTriple({36, 36, 36}));
    CHECK(!ValidateTdpTriple({5, 4, 5}, &error));
    CHECK(error.find(L"FAST") != std::wstring::npos);
    CHECK(!ValidateTdpTriple({5, 36, 5}));
    CHECK(!ValidateTdpTriple({5, 5, 4}, &error));
    CHECK(error.find(L"SLOW") != std::wstring::npos);
    CHECK(!ValidateTdpTriple({5, 5, 36}));
    CHECK(!ValidateTdpTriple({21, 20, 25}, &error));
    CHECK(error.find(L"FAST") != std::wstring::npos);
    CHECK(!ValidateTdpTriple(21, 25, 20, &error));
    CHECK(error.find(L"SLOW") != std::wstring::npos);
    CHECK(!ValidateTdpTriple(10, 15, 20, &error));
    CHECK(error.find(L"SLOW") != std::wstring::npos && error.find(L"FAST") != std::wstring::npos);
    CHECK((NormalizeTdpHierarchy({20, 10, 5}) == TdpTriple{20, 20, 20}));
    CHECK((NormalizeTdpHierarchy({10, 15, 20}) == TdpTriple{10, 20, 20}));
    CHECK((NormalizeTdpHierarchy({10, 30, 20}) == TdpTriple{10, 30, 20}));
    CHECK((TdpTriple{5, 6, 7} == TdpTriple{5, 6, 7}));
    CHECK((TdpTriple{5, 6, 7} != TdpTriple{5, 7, 7}));
}

void TestFanCurve() {
    FanCurve baseline{{44,48,48,51,51,55,60,71,87,87}};
    std::wstring error = L"stale";
    CHECK(ValidateFanCurve(baseline, &error));
    CHECK(error.empty());
    CHECK((baseline == FanCurve{{44,48,48,51,51,55,60,71,87,87}}));
    CHECK((baseline != FanCurve{{44,48,48,51,51,55,60,71,87,88}}));
    CHECK(!ValidateFanCurve(FanCurve{{19,20,30,40,50,60,70,80,90,100}}, &error));
    CHECK(error.find(L"20%") != std::wstring::npos);
    CHECK(!ValidateFanCurve(FanCurve{{30,30,30,29,50,60,70,80,90,100}}, &error));
    CHECK(error.find(L"decrease") != std::wstring::npos);
    CHECK(!ValidateFanCurve(FanCurve{{20,20,20,20,20,20,20,59,80,85}}, &error));
    CHECK(error.find(L"Safety") != std::wstring::npos);
    CHECK(InterpolateFanDuty(baseline, 5) == 44);
    CHECK(InterpolateFanDuty(baseline, 45) == 51);
    CHECK(InterpolateFanDuty(baseline, 65) == 58);
    CHECK(InterpolateFanDuty(baseline, 105) == 87);
    CHECK(EstimateFanRpm(28) == 2100);
    CHECK(EstimateFanRpm(84) == 6300);
}

void TestPathNormalization() {
    CHECK(NormalizeWindowsPath(L"").empty());
    CHECK(NormalizeWindowsPath(L"C:/Games/Foo.EXE") == L"c:\\games\\foo.exe");
    CHECK(NormalizeWindowsPath(L"c:\\\\games\\.\\sub\\..\\foo.exe\\") ==
          L"c:\\games\\foo.exe");
    CHECK(NormalizeWindowsPath(L"C:Games\\foo.exe") == L"c:games\\foo.exe");
    CHECK(NormalizeWindowsPath(L"..\\Games\\..\\foo.exe") == L"..\\foo.exe");
    CHECK(NormalizeWindowsPath(L"\\a\\..\\foo.exe") == L"\\foo.exe");
    CHECK(NormalizeWindowsPath(L"\\\\Server\\Share\\folder\\..\\Game.exe") ==
          L"\\\\server\\share\\game.exe");
    CHECK(NormalizeWindowsPath(L"\\\\Server\\Share\\..\\Game.exe") ==
          L"\\\\server\\share\\game.exe");
    CHECK(NormalizeWindowsPath(L"\\\\?\\C:\\Games\\GAME.EXE") ==
          L"c:\\games\\game.exe");
    CHECK(NormalizeWindowsPath(L"\\\\?\\UNC\\Server\\Share\\Game.exe") ==
          L"\\\\server\\share\\game.exe");

    CHECK(WindowsPathsEqual(L"C:\\Games\\Game.exe", L"c:/games/./GAME.EXE"));
    CHECK(WindowsPathsEqual(L"\\\\server\\share\\a.exe",
                            L"//SERVER/share/folder/../a.exe"));
    CHECK(!WindowsPathsEqual(L"C:\\A\\game.exe", L"C:\\B\\game.exe"));
    CHECK(!WindowsPathsEqual(L"C:\\A\\notgame.exe.bak", L"C:\\A\\notgame.exe"));
    CHECK(WindowsPathsEqual(L"", L""));
    CHECK(!WindowsPathsEqual(L"", L"."));

    const GameProfile profile = Profile(L"Game", L" C:\\Games\\Game.exe ");
    CHECK(ProfileMatchesProcess(profile, L"c:/games/GAME.exe"));
    CHECK(!ProfileMatchesProcess(profile, L"D:\\Games\\Game.exe"));
    CHECK(!ProfileMatchesProcess(Profile(L"Empty", L""), L"C:\\game.exe"));
}

bool HasIssue(const std::vector<ProfileValidationIssue>& issues,
              ProfileValidationError error, std::size_t index,
              std::size_t duplicateOf = kNoProfile) {
    for (const ProfileValidationIssue& issue : issues) {
        if (issue.error == error && issue.profileIndex == index &&
            (duplicateOf == kNoProfile || issue.duplicateOf == duplicateOf)) {
            return true;
        }
    }
    return false;
}

void TestProfileValidation() {
    const std::vector<GameProfile> valid = {
        Profile(L"Alpha", L"C:\\Games\\Alpha.exe"),
        Profile(L"Beta", L"D:/Games/Beta.EXE", {10, 15, 12})
    };
    CHECK(ValidateGameProfiles(valid).empty());

    std::wstring error = L"stale";
    std::size_t index = 123;
    CHECK(ValidateGameProfiles(valid, &error, &index));
    CHECK(error.empty());
    CHECK(index == kNoProfile);

    const std::vector<GameProfile> invalid = {
        Profile(L"  ", L"  "),
        Profile(L"Alpha", L"C:\\Games\\Alpha.com", {4, 10, 10}),
        Profile(L" alpha ", L"C:\\Games\\same.exe", {10, 10, 10}),
        Profile(L"Other", L"c:/games/./SAME.EXE", {10, 10, 10}),
        Profile(L"Trailing", L"C:\\Games\\folder.exe\\", {10, 10, 10}),
        Profile(L"Relative", L"game.exe", {10, 10, 10}),
        Profile(L"Incomplete UNC", L"\\\\server\\game.exe", {10, 10, 10})
    };
    const std::vector<ProfileValidationIssue> issues = ValidateGameProfiles(invalid);
    CHECK(HasIssue(issues, ProfileValidationError::EmptyName, 0));
    CHECK(HasIssue(issues, ProfileValidationError::EmptyPath, 0));
    CHECK(HasIssue(issues, ProfileValidationError::PathMustEndInExe, 1));
    CHECK(HasIssue(issues, ProfileValidationError::InvalidTdp, 1));
    CHECK(HasIssue(issues, ProfileValidationError::DuplicateName, 2, 1));
    CHECK(HasIssue(issues, ProfileValidationError::DuplicatePath, 3, 2));
    CHECK(HasIssue(issues, ProfileValidationError::PathMustEndInExe, 4));
    CHECK(HasIssue(issues, ProfileValidationError::PathMustBeAbsolute, 5));
    CHECK(HasIssue(issues, ProfileValidationError::PathMustBeAbsolute, 6));

    CHECK(!ValidateGameProfiles(invalid, &error, &index));
    CHECK(index == 0);
    CHECK(!error.empty());

    // Exact full paths are required; equal basenames in different directories
    // are not duplicates.
    CHECK(ValidateGameProfiles({
        Profile(L"One", L"C:\\One\\game.exe"),
        Profile(L"Two", L"C:\\Two\\game.exe")
    }).empty());
    CHECK(ValidateGameProfiles({
        Profile(L"UNC", L"\\\\server\\share\\game.exe"),
        Profile(L"Extended", L"\\\\?\\C:\\Games\\extended.exe")
    }).empty());
    GameProfile fpsProfile = Profile(L"Limited", L"C:\\Games\\limited.exe");
    fpsProfile.fpsLimitEnabled = true; fpsProfile.fpsLimit = 30;
    CHECK(ValidateGameProfiles({fpsProfile}).empty());
    fpsProfile.fpsLimit = 144;
    CHECK(ValidateGameProfiles({fpsProfile}).empty());
    fpsProfile.fpsLimit = 29;
    CHECK(HasIssue(ValidateGameProfiles({fpsProfile}), ProfileValidationError::InvalidFpsLimit, 0));
    fpsProfile.fpsLimit = 145;
    CHECK(HasIssue(ValidateGameProfiles({fpsProfile}), ProfileValidationError::InvalidFpsLimit, 0));
}

void TestArbiter() {
    const std::vector<GameProfile> profiles = {
        Profile(L"First", L"C:\\Games\\first.exe", {10, 10, 10}),
        Profile(L"Second", L"D:\\Games\\second.exe", {20, 20, 20}),
        Profile(L"Third", L"E:\\Games\\third.exe", {25, 30, 28})
    };

    CHECK(ArbitrateProfile(profiles, {}) == kNoProfile);
    CHECK(ArbitrateProfile(profiles, {L"C:\\Other\\first.exe"}) == kNoProfile);
    CHECK(ArbitrateProfile(profiles, {L"d:/games/SECOND.EXE"}) == 1);

    // Profile order, not process enumeration order, breaks a fresh tie.
    CHECK(ArbitrateProfile(profiles,
          {L"E:\\Games\\third.exe", L"D:\\Games\\second.exe"}) == 1);
    CHECK(ArbitrateProfile(profiles,
          {L"D:\\Games\\second.exe", L"E:\\Games\\third.exe"}) == 1);

    // A current profile is sticky while it remains present.
    CHECK(ArbitrateProfile(profiles,
          {L"D:\\Games\\second.exe", L"E:\\Games\\third.exe"}, 2) == 2);
    CHECK(ArbitrateProfile(profiles, {L"D:\\Games\\second.exe"}, 2) == 1);
    CHECK(ArbitrateProfile(profiles, {L"D:\\Games\\second.exe"}, 999) == 1);

    const auto selected = ArbitrateProfileOptional(
        profiles, {L"c:/games/first.exe"}, std::nullopt);
    CHECK(selected.has_value());
    CHECK(selected.value_or(kNoProfile) == 0);
    CHECK(!ArbitrateProfileOptional(profiles, {}, 0).has_value());
}

void TestProcessFamilyTracking() {
    const std::vector<GameProfile> profiles = {
        Profile(L"RetroBat", L"C:\\RetroBat\\retrobat.exe", {10, 12, 11}),
        Profile(L"Other", L"D:\\Games\\other.exe", {15, 20, 18})
    };
    ProcessFamilyTracker tracker;
    auto active = tracker.Update({
        {{100, 1000}, 1, L"C:\\RetroBat\\RetroBat.exe"},
        {{200, 1100}, 100, L"C:\\RetroBat\\emulationstation.exe"},
        {{300, 1200}, 200, L"C:\\RetroBat\\emulators\\retroarch.exe"}
    }, profiles, 1000);
    CHECK(active.size() == 1 && active[0] == 0);

    // The launcher and intermediate process may exit; the descendant remains
    // attached to the already-observed family.
    active = tracker.Update({{{300, 1200}, 200, L"C:\\RetroBat\\emulators\\retroarch.exe"}}, profiles, 1750);
    CHECK(active.size() == 1 && active[0] == 0);
    active = tracker.Update({}, profiles, 2500);
    CHECK(active.empty());

    // A child discovered immediately after its observed parent exited inherits.
    ProcessFamilyTracker exitedParentTracker;
    CHECK(exitedParentTracker.Update({{{400, 4000}, 1, L"C:\\RetroBat\\retrobat.exe"}}, profiles, 4000).size() == 1);
    active = exitedParentTracker.Update({{{500, 4100}, 400, L"C:\\RetroBat\\emulationstation.exe"}}, profiles, 4500);
    CHECK(active.size() == 1 && active[0] == 0);

    // PID reuse must not attach children of a different current process.
    active = exitedParentTracker.Update({
        {{400, 9000}, 1, L"C:\\Windows\\unrelated.exe"},
        {{600, 9100}, 400, L"C:\\Windows\\child.exe"}
    }, profiles, 9000);
    CHECK(active.empty());

    // If the root was never observed, a matching basename or descendant path
    // alone cannot activate the family.
    ProcessFamilyTracker coldTracker;
    active = coldTracker.Update({{{700, 7000}, 999, L"C:\\RetroBat\\emulationstation.exe"}}, profiles, 7000);
    CHECK(active.empty());
}

void TestTargetSemantics() {
    const TdpTriple base{12, 16, 14};
    const std::vector<GameProfile> profiles = {
        Profile(L"One", L"C:\\one.exe", {20, 25, 22}),
        Profile(L"Same watts", L"C:\\same.exe", base)
    };

    const TdpTarget baseTarget = MakeBaseTarget(base);
    CHECK(baseTarget.kind == TdpTargetKind::Base);
    CHECK(baseTarget.profileIndex == kNoProfile);
    CHECK(baseTarget.tdp == base);
    CHECK(SameTargetIdentity(baseTarget, ResolveTdpTarget(base, profiles, kNoProfile)));
    CHECK(SameTargetIdentity(baseTarget,
          ResolveTdpTarget(base, profiles, std::optional<std::size_t>{})));
    CHECK(SameTargetIdentity(baseTarget, ResolveTdpTarget(base, profiles, 99)));

    const TdpTarget profileTarget = ResolveTdpTarget(base, profiles, 0);
    CHECK(profileTarget.kind == TdpTargetKind::Profile);
    CHECK(profileTarget.profileIndex == 0);
    CHECK(profileTarget.tdp == profiles[0].tdp);
    CHECK(TargetValueChanged(baseTarget, profileTarget));
    CHECK(!SameTargetIdentity(baseTarget, profileTarget));

    const TdpTarget sameValueProfile = ResolveTdpTarget(base, profiles, 1);
    CHECK(sameValueProfile.kind == TdpTargetKind::Profile);
    CHECK(!TargetValueChanged(baseTarget, sameValueProfile));
    CHECK(!SameTargetIdentity(baseTarget, sameValueProfile));
    CHECK(SameTargetIdentity(sameValueProfile, MakeProfileTarget(profiles[1], 1)));

    // A changed base is retained but does not override an active profile.
    const TdpTriple changedBase{8, 10, 9};
    CHECK(ResolveTdpTarget(changedBase, profiles, 0).tdp == profiles[0].tdp);
    CHECK(ResolveTdpTarget(changedBase, profiles, kNoProfile).tdp == changedBase);
}

} // namespace

int main() {
    TestTdpValidation();
    TestFanCurve();
    TestPathNormalization();
    TestProfileValidation();
    TestArbiter();
    TestProcessFamilyTracking();
    TestTargetSemantics();

    if (failures != 0) {
        std::cerr << failures << " of " << checks << " checks failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "LegionGoCore: " << checks << " checks passed.\n";
    return EXIT_SUCCESS;
}
