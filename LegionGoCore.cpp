#include "LegionGoCore.h"

#include <algorithm>
#include <cwctype>
#include <set>
#include <unordered_map>
#include <utility>

namespace LegionGoCore {
namespace {

std::wstring Trim(const std::wstring& text) {
    std::size_t first = 0;
    while (first < text.size() &&
           (std::iswspace(static_cast<wint_t>(text[first])) != 0 || text[first] == 0xFEFF)) {
        ++first;
    }

    std::size_t last = text.size();
    while (last > first && std::iswspace(static_cast<wint_t>(text[last - 1])) != 0) {
        --last;
    }
    return text.substr(first, last - first);
}

wchar_t FoldCase(wchar_t value) {
    // Windows paths encountered here are overwhelmingly ASCII.  towlower also
    // gives deterministic behavior for every character in the active C locale
    // without introducing a dependency on Win32's locale APIs.
    return static_cast<wchar_t>(std::towlower(static_cast<wint_t>(value)));
}

std::wstring FoldCase(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t ch) { return FoldCase(ch); });
    return value;
}

bool IsSlash(wchar_t value) {
    return value == L'\\' || value == L'/';
}

bool IsAsciiDriveLetter(wchar_t value) {
    return (value >= L'a' && value <= L'z') || (value >= L'A' && value <= L'Z');
}

bool IsAbsoluteWindowsPath(const std::wstring& originalPath) {
    if (originalPath.empty()) {
        return false;
    }

    std::wstring path = originalPath;
    std::replace(path.begin(), path.end(), L'/', L'\\');
    if (path.size() >= 8 && FoldCase(path.substr(0, 8)) == L"\\\\?\\unc\\") {
        path = L"\\\\" + path.substr(8);
    } else if (path.size() >= 7 && path.compare(0, 4, L"\\\\?\\") == 0 &&
               IsAsciiDriveLetter(path[4]) && path[5] == L':' && IsSlash(path[6])) {
        path.erase(0, 4);
    }

    if (path.size() >= 3 && IsAsciiDriveLetter(path[0]) && path[1] == L':' &&
        IsSlash(path[2])) {
        return true;
    }
    if (path.size() < 5 || !IsSlash(path[0]) || !IsSlash(path[1])) {
        return false;
    }

    std::size_t serverStart = 2;
    while (serverStart < path.size() && IsSlash(path[serverStart])) {
        ++serverStart;
    }
    const std::size_t serverEnd = path.find(L'\\', serverStart);
    if (serverEnd == std::wstring::npos || serverEnd == serverStart) {
        return false;
    }
    const std::wstring server = path.substr(serverStart, serverEnd - serverStart);
    if (server == L"?" || server == L".") {
        return false;
    }

    std::size_t shareStart = serverEnd + 1;
    while (shareStart < path.size() && IsSlash(path[shareStart])) {
        ++shareStart;
    }
    const std::size_t shareEnd = path.find(L'\\', shareStart);
    if (shareStart >= path.size() || shareEnd == std::wstring::npos || shareEnd == shareStart) {
        return false;
    }
    std::size_t fileStart = shareEnd + 1;
    while (fileStart < path.size() && IsSlash(path[fileStart])) {
        ++fileStart;
    }
    return fileStart < path.size();
}

bool EndsInExe(const std::wstring& path) {
    if (path.empty() || IsSlash(path.back())) {
        return false;
    }

    const std::wstring key = NormalizeWindowsPath(path);
    const std::size_t separator = key.find_last_of(L'\\');
    const std::wstring fileName =
        separator == std::wstring::npos ? key : key.substr(separator + 1);
    return fileName.size() > 4 &&
           fileName.compare(fileName.size() - 4, 4, L".exe") == 0;
}

void SetError(std::wstring* error, const std::wstring& message) {
    if (error != nullptr) {
        *error = message;
    }
}

} // namespace

bool operator==(const TdpTriple& left, const TdpTriple& right) noexcept {
    return left.stapm == right.stapm && left.fast == right.fast && left.slow == right.slow;
}

bool operator!=(const TdpTriple& left, const TdpTriple& right) noexcept {
    return !(left == right);
}

bool ValidateTdpTriple(const TdpTriple& value, std::wstring* error) {
    if (value.stapm < kMinimumTdpWatts || value.stapm > kMaximumTdpWatts) {
        SetError(error, L"STAPM must be between 5 and 35 watts.");
        return false;
    }
    if (value.fast < kMinimumTdpWatts || value.fast > kMaximumTdpWatts) {
        SetError(error, L"FAST must be between 5 and 35 watts.");
        return false;
    }
    if (value.slow < kMinimumTdpWatts || value.slow > kMaximumTdpWatts) {
        SetError(error, L"SLOW must be between 5 and 35 watts.");
        return false;
    }
    if (value.stapm > value.fast) {
        SetError(error, L"STAPM must not exceed FAST.");
        return false;
    }
    if (value.stapm > value.slow) {
        SetError(error, L"STAPM must not exceed SLOW.");
        return false;
    }
    if (value.slow > value.fast) {
        SetError(error, L"SLOW must not exceed FAST (required order: STAPM <= SLOW <= FAST).");
        return false;
    }

    SetError(error, L"");
    return true;
}

bool ValidateTdpTriple(int stapm, int fast, int slow, std::wstring* error) {
    return ValidateTdpTriple(TdpTriple{stapm, fast, slow}, error);
}

TdpTriple NormalizeTdpHierarchy(TdpTriple value) noexcept {
    value.slow = (std::max)(value.slow, value.stapm);
    value.fast = (std::max)(value.fast, (std::max)(value.slow, value.stapm));
    return value;
}

bool operator==(const FanCurve& left, const FanCurve& right) noexcept {
    return left.dutyPercent == right.dutyPercent;
}
bool operator!=(const FanCurve& left, const FanCurve& right) noexcept { return !(left == right); }

bool ValidateFanCurve(const FanCurve& curve, std::wstring* error) {
    for (std::size_t index = 0; index < curve.dutyPercent.size(); ++index) {
        const int duty = curve.dutyPercent[index];
        if (duty < 20 || duty > 100) {
            SetError(error, L"Every fan point must be between 20% and 100%.");
            return false;
        }
        if (index != 0 && duty < curve.dutyPercent[index - 1]) {
            SetError(error, L"Fan duty must not decrease as temperature rises.");
            return false;
        }
    }
    if (curve.dutyPercent[7] < 60 || curve.dutyPercent[8] < 80 || curve.dutyPercent[9] < 85) {
        SetError(error, L"Safety minimums are 60% at 80 C, 80% at 90 C, and 85% at 100 C.");
        return false;
    }
    SetError(error, L"");
    return true;
}

int InterpolateFanDuty(const FanCurve& curve, int temperatureC) noexcept {
    if (temperatureC <= kFanTemperaturesC.front()) return curve.dutyPercent.front();
    if (temperatureC >= kFanTemperaturesC.back()) return curve.dutyPercent.back();
    for (std::size_t index = 1; index < kFanPointCount; ++index) {
        if (temperatureC <= kFanTemperaturesC[index]) {
            const int lowTemperature = kFanTemperaturesC[index - 1];
            const int span = kFanTemperaturesC[index] - lowTemperature;
            const int lowDuty = curve.dutyPercent[index - 1];
            const int delta = curve.dutyPercent[index] - lowDuty;
            return lowDuty + (delta * (temperatureC - lowTemperature) + span / 2) / span;
        }
    }
    return curve.dutyPercent.back();
}

int EstimateFanRpm(int dutyPercent) noexcept {
    return (std::max)(0, (std::min)(100, dutyPercent)) * 75;
}

std::wstring NormalizeWindowsPath(const std::wstring& originalPath) {
    if (originalPath.empty()) {
        return {};
    }

    std::wstring path = originalPath;
    std::replace(path.begin(), path.end(), L'/', L'\\');

    // Win32 and NT extended forms can describe the same ordinary drive or UNC
    // path returned by another process-enumeration API.
    if (path.size() >= 8 && FoldCase(path.substr(0, 8)) == L"\\\\?\\unc\\") {
        path = L"\\\\" + path.substr(8);
    } else if (path.size() >= 7 && path.compare(0, 4, L"\\\\?\\") == 0 &&
               IsAsciiDriveLetter(path[4]) && path[5] == L':' && IsSlash(path[6])) {
        path.erase(0, 4);
    }

    enum class RootKind { Relative, Rooted, DriveRelative, DriveRooted, Unc };
    RootKind root = RootKind::Relative;
    std::wstring drive;
    std::size_t cursor = 0;

    if (path.size() >= 2 && IsSlash(path[0]) && IsSlash(path[1])) {
        root = RootKind::Unc;
        cursor = 2;
        while (cursor < path.size() && IsSlash(path[cursor])) {
            ++cursor;
        }
    } else if (path.size() >= 2 && IsAsciiDriveLetter(path[0]) && path[1] == L':') {
        drive.assign(1, FoldCase(path[0]));
        drive.push_back(L':');
        cursor = 2;
        if (cursor < path.size() && IsSlash(path[cursor])) {
            root = RootKind::DriveRooted;
            while (cursor < path.size() && IsSlash(path[cursor])) {
                ++cursor;
            }
        } else {
            root = RootKind::DriveRelative;
        }
    } else if (IsSlash(path[0])) {
        root = RootKind::Rooted;
        cursor = 1;
        while (cursor < path.size() && IsSlash(path[cursor])) {
            ++cursor;
        }
    }

    std::vector<std::wstring> components;
    while (cursor <= path.size()) {
        const std::size_t end = path.find(L'\\', cursor);
        const std::size_t count =
            end == std::wstring::npos ? path.size() - cursor : end - cursor;
        std::wstring component = FoldCase(path.substr(cursor, count));

        if (!component.empty() && component != L".") {
            const std::size_t protectedComponents = root == RootKind::Unc ? 2U : 0U;
            if (component == L"..") {
                if (components.size() > protectedComponents && components.back() != L"..") {
                    components.pop_back();
                } else if (root == RootKind::Relative || root == RootKind::DriveRelative) {
                    components.push_back(std::move(component));
                }
            } else {
                components.push_back(std::move(component));
            }
        }

        if (end == std::wstring::npos) {
            break;
        }
        cursor = end + 1;
        while (cursor < path.size() && IsSlash(path[cursor])) {
            ++cursor;
        }
    }

    std::wstring result;
    switch (root) {
        case RootKind::Rooted:
            result = L"\\";
            break;
        case RootKind::DriveRelative:
            result = drive;
            break;
        case RootKind::DriveRooted:
            result = drive + L"\\";
            break;
        case RootKind::Unc:
            result = L"\\\\";
            break;
        case RootKind::Relative:
            break;
    }

    for (std::size_t index = 0; index < components.size(); ++index) {
        const bool driveRelativeFirst = root == RootKind::DriveRelative && index == 0;
        const bool alreadySeparated = !result.empty() && result.back() == L'\\';
        if (!result.empty() && !alreadySeparated && !driveRelativeFirst) {
            result.push_back(L'\\');
        }
        result += components[index];
    }
    return result;
}

bool WindowsPathsEqual(const std::wstring& left, const std::wstring& right) {
    if (left.empty() || right.empty()) {
        return left.empty() && right.empty();
    }
    return NormalizeWindowsPath(left) == NormalizeWindowsPath(right);
}

bool ProfileMatchesProcess(const GameProfile& profile, const std::wstring& processPath) {
    const std::wstring profilePath = Trim(profile.path);
    return !profilePath.empty() && !processPath.empty() &&
           WindowsPathsEqual(profilePath, processPath);
}

std::vector<ProfileValidationIssue> ValidateGameProfiles(
    const std::vector<GameProfile>& profiles) {
    std::vector<ProfileValidationIssue> issues;
    std::unordered_map<std::wstring, std::size_t> names;
    std::unordered_map<std::wstring, std::size_t> paths;

    for (std::size_t index = 0; index < profiles.size(); ++index) {
        const GameProfile& profile = profiles[index];
        const std::wstring name = Trim(profile.name);
        const std::wstring path = Trim(profile.path);

        if (name.empty()) {
            issues.push_back({ProfileValidationError::EmptyName, index, kNoProfile,
                              L"Profile name must not be empty."});
        } else {
            const std::wstring nameKey = FoldCase(name);
            const auto inserted = names.emplace(nameKey, index);
            if (!inserted.second) {
                issues.push_back({ProfileValidationError::DuplicateName, index,
                                  inserted.first->second,
                                  L"Profile name duplicates an earlier profile."});
            }
        }

        if (path.empty()) {
            issues.push_back({ProfileValidationError::EmptyPath, index, kNoProfile,
                              L"Profile executable path must not be empty."});
        } else {
            if (!IsAbsoluteWindowsPath(path)) {
                issues.push_back({ProfileValidationError::PathMustBeAbsolute, index, kNoProfile,
                                  L"Profile executable path must be absolute."});
            }
            if (!EndsInExe(path)) {
                issues.push_back({ProfileValidationError::PathMustEndInExe, index, kNoProfile,
                                  L"Profile path must end in .exe."});
            }

            const std::wstring pathKey = NormalizeWindowsPath(path);
            const auto inserted = paths.emplace(pathKey, index);
            if (!inserted.second) {
                issues.push_back({ProfileValidationError::DuplicatePath, index,
                                  inserted.first->second,
                                  L"Profile executable path duplicates an earlier profile."});
            }
        }

        std::wstring tdpError;
        if (!ValidateTdpTriple(profile.tdp, &tdpError)) {
            issues.push_back({ProfileValidationError::InvalidTdp, index, kNoProfile,
                              L"Invalid profile TDP: " + tdpError});
        }
    }
    return issues;
}

bool ValidateGameProfiles(const std::vector<GameProfile>& profiles,
                          std::wstring* error,
                          std::size_t* profileIndex) {
    const std::vector<ProfileValidationIssue> issues = ValidateGameProfiles(profiles);
    if (issues.empty()) {
        SetError(error, L"");
        if (profileIndex != nullptr) {
            *profileIndex = kNoProfile;
        }
        return true;
    }

    SetError(error, issues.front().message);
    if (profileIndex != nullptr) {
        *profileIndex = issues.front().profileIndex;
    }
    return false;
}

std::size_t ArbitrateProfile(const std::vector<GameProfile>& profiles,
                             const std::vector<std::wstring>& runningProcessPaths,
                             std::size_t currentProfile) {
    std::vector<std::wstring> runningKeys;
    runningKeys.reserve(runningProcessPaths.size());
    for (const std::wstring& processPath : runningProcessPaths) {
        if (!processPath.empty()) {
            runningKeys.push_back(NormalizeWindowsPath(processPath));
        }
    }

    const auto isRunning = [&runningKeys](const GameProfile& profile) {
        const std::wstring path = Trim(profile.path);
        if (path.empty()) {
            return false;
        }
        const std::wstring key = NormalizeWindowsPath(path);
        return std::find(runningKeys.begin(), runningKeys.end(), key) != runningKeys.end();
    };

    if (currentProfile < profiles.size() && isRunning(profiles[currentProfile])) {
        return currentProfile;
    }
    for (std::size_t index = 0; index < profiles.size(); ++index) {
        if (isRunning(profiles[index])) {
            return index;
        }
    }
    return kNoProfile;
}

std::optional<std::size_t> ArbitrateProfileOptional(
    const std::vector<GameProfile>& profiles,
    const std::vector<std::wstring>& runningProcessPaths,
    std::optional<std::size_t> currentProfile) {
    const std::size_t selected = ArbitrateProfile(
        profiles, runningProcessPaths, currentProfile.value_or(kNoProfile));
    if (selected == kNoProfile) {
        return std::nullopt;
    }
    return selected;
}

bool ProcessIdentity::operator<(const ProcessIdentity& other) const noexcept {
    return pid < other.pid || (pid == other.pid && creation < other.creation);
}

std::vector<std::size_t> ProcessFamilyTracker::Update(
    const std::vector<ProcessSample>& processes,
    const std::vector<GameProfile>& profiles,
    std::uint64_t nowMilliseconds) {
    std::set<std::wstring> validProfileKeys;
    for (const auto& profile : profiles) validProfileKeys.insert(NormalizeWindowsPath(profile.path));
    for (auto it = members_.begin(); it != members_.end();) {
        if (!validProfileKeys.count(it->second.profilePathKey) ||
            nowMilliseconds - it->second.lastSeen > 30000) it = members_.erase(it);
        else ++it;
    }

    std::map<std::uint32_t, const ProcessSample*> currentByPid;
    for (const auto& process : processes) currentByPid[process.identity.pid] = &process;
    for (const auto& process : processes) {
        auto member = members_.find(process.identity);
        if (member != members_.end()) member->second.lastSeen = nowMilliseconds;
    }

    for (const auto& process : processes) {
        if (process.path.empty()) continue;
        for (const auto& profile : profiles) {
            if (ProfileMatchesProcess(profile, process.path)) {
                members_[process.identity] = {NormalizeWindowsPath(profile.path), nowMilliseconds};
                break;
            }
        }
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& process : processes) {
            if (members_.find(process.identity) != members_.end() || process.parentPid == 0) continue;
            std::wstring inherited;
            const auto currentParent = currentByPid.find(process.parentPid);
            if (currentParent != currentByPid.end()) {
                const auto parentMember = members_.find(currentParent->second->identity);
                if (parentMember != members_.end()) inherited = parentMember->second.profilePathKey;
            } else {
                std::uint64_t newestCreation = 0;
                for (const auto& member : members_) {
                    if (member.first.pid == process.parentPid &&
                        member.first.creation <= process.identity.creation &&
                        nowMilliseconds - member.second.lastSeen <= 5000 &&
                        member.first.creation >= newestCreation) {
                        inherited = member.second.profilePathKey;
                        newestCreation = member.first.creation;
                    }
                }
            }
            if (!inherited.empty()) {
                members_[process.identity] = {inherited, nowMilliseconds};
                changed = true;
            }
        }
    }

    std::set<std::wstring> activeKeys;
    for (const auto& process : processes) {
        const auto member = members_.find(process.identity);
        if (member != members_.end()) activeKeys.insert(member->second.profilePathKey);
    }
    std::vector<std::size_t> activeProfiles;
    for (std::size_t index = 0; index < profiles.size(); ++index) {
        if (activeKeys.count(NormalizeWindowsPath(profiles[index].path))) activeProfiles.push_back(index);
    }
    return activeProfiles;
}

TdpTarget MakeBaseTarget(const TdpTriple& base) {
    return TdpTarget{base, TdpTargetKind::Base, kNoProfile};
}

TdpTarget MakeProfileTarget(const GameProfile& profile, std::size_t profileIndex) {
    return TdpTarget{profile.tdp, TdpTargetKind::Profile, profileIndex};
}

TdpTarget ResolveTdpTarget(const TdpTriple& base,
                           const std::vector<GameProfile>& profiles,
                           std::size_t activeProfile) {
    if (activeProfile < profiles.size()) {
        return MakeProfileTarget(profiles[activeProfile], activeProfile);
    }
    return MakeBaseTarget(base);
}

TdpTarget ResolveTdpTarget(const TdpTriple& base,
                           const std::vector<GameProfile>& profiles,
                           std::optional<std::size_t> activeProfile) {
    return ResolveTdpTarget(base, profiles, activeProfile.value_or(kNoProfile));
}

bool SameTargetIdentity(const TdpTarget& left, const TdpTarget& right) noexcept {
    return left.tdp == right.tdp && left.kind == right.kind &&
           left.profileIndex == right.profileIndex;
}

bool TargetValueChanged(const TdpTarget& applied, const TdpTarget& desired) noexcept {
    return applied.tdp != desired.tdp;
}

} // namespace LegionGoCore
