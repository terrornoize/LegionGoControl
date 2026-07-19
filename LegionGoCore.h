#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <vector>

// Platform-independent policy used by the Windows front end.  This header does
// not include Windows headers, so it can also be built by the core test binary.
namespace LegionGoCore {

inline constexpr int kMinimumTdpWatts = 5;
inline constexpr int kMaximumTdpWatts = 35;
inline constexpr std::size_t kNoProfile = (std::numeric_limits<std::size_t>::max)();

struct TdpTriple {
    int stapm = 0;
    int fast = 0;
    int slow = 0;
};

bool operator==(const TdpTriple& left, const TdpTriple& right) noexcept;
bool operator!=(const TdpTriple& left, const TdpTriple& right) noexcept;

// A valid triple has every value in [5, 35] and follows the conventional AMD
// hierarchy STAPM <= SLOW <= FAST. On success, error (when supplied) is cleared.
bool ValidateTdpTriple(const TdpTriple& value, std::wstring* error = nullptr);
bool ValidateTdpTriple(int stapm, int fast, int slow, std::wstring* error = nullptr);
// Raises dependent limits only: SLOW becomes at least STAPM and FAST becomes
// at least both. It does not clamp the overall 5-35 W range.
TdpTriple NormalizeTdpHierarchy(TdpTriple value) noexcept;

struct GameProfile {
    std::wstring name;
    std::wstring path;
    TdpTriple tdp;
};

// Produces a lexical Windows path key: slash styles, redundant separators,
// dot components, drive-letter/case differences, and ordinary extended path
// prefixes are normalized.  It deliberately does not touch the file system,
// expand environment variables, or perform partial/basename matching.
std::wstring NormalizeWindowsPath(const std::wstring& path);
bool WindowsPathsEqual(const std::wstring& left, const std::wstring& right);
bool ProfileMatchesProcess(const GameProfile& profile, const std::wstring& processPath);

// Profile validation is intentionally independent of the file system.  A path
// must be an absolute, nonblank .exe path and be unique by normalized full path.
// Names must be nonblank and unique case-insensitively.  TDP is also checked.
enum class ProfileValidationError {
    EmptyName,
    EmptyPath,
    PathMustBeAbsolute,
    PathMustEndInExe,
    DuplicateName,
    DuplicatePath,
    InvalidTdp
};

struct ProfileValidationIssue {
    ProfileValidationError error = ProfileValidationError::EmptyName;
    std::size_t profileIndex = 0;
    std::size_t duplicateOf = kNoProfile;
    std::wstring message;
};

std::vector<ProfileValidationIssue> ValidateGameProfiles(
    const std::vector<GameProfile>& profiles);

// Convenience form for callers that only need the first diagnostic.
bool ValidateGameProfiles(
    const std::vector<GameProfile>& profiles,
    std::wstring* error,
    std::size_t* profileIndex = nullptr);

// Selects by exact normalized executable path.  Selection is independent of
// process enumeration order: a still-running current profile wins; otherwise
// the first matching profile in configuration order wins.
std::size_t ArbitrateProfile(
    const std::vector<GameProfile>& profiles,
    const std::vector<std::wstring>& runningProcessPaths,
    std::size_t currentProfile = kNoProfile);

std::optional<std::size_t> ArbitrateProfileOptional(
    const std::vector<GameProfile>& profiles,
    const std::vector<std::wstring>& runningProcessPaths,
    std::optional<std::size_t> currentProfile = std::nullopt);

struct ProcessIdentity {
    std::uint32_t pid = 0;
    std::uint64_t creation = 0;
    bool operator<(const ProcessIdentity& other) const noexcept;
};

struct ProcessSample {
    ProcessIdentity identity;
    std::uint32_t parentPid = 0;
    std::wstring path;
};

// Automatically keeps a profile active for descendants of an observed root.
// Dead parents are retained briefly so children discovered in the next poll
// still inherit; PID+creation prevents PID reuse from joining an old family.
class ProcessFamilyTracker {
public:
    std::vector<std::size_t> Update(
        const std::vector<ProcessSample>& processes,
        const std::vector<GameProfile>& profiles,
        std::uint64_t nowMilliseconds);

private:
    struct Membership {
        std::wstring profilePathKey;
        std::uint64_t lastSeen = 0;
    };
    std::map<ProcessIdentity, Membership> members_;
};

enum class TdpTargetKind {
    Base,
    Profile
};

// The source is retained even when base and profile wattages happen to match.
// profileIndex is kNoProfile for a base target.
struct TdpTarget {
    TdpTriple tdp;
    TdpTargetKind kind = TdpTargetKind::Base;
    std::size_t profileIndex = kNoProfile;
};

TdpTarget MakeBaseTarget(const TdpTriple& base);
TdpTarget MakeProfileTarget(const GameProfile& profile, std::size_t profileIndex);

// An active, in-range profile overrides base; no/invalid profile restores base.
TdpTarget ResolveTdpTarget(
    const TdpTriple& base,
    const std::vector<GameProfile>& profiles,
    std::size_t activeProfile = kNoProfile);

TdpTarget ResolveTdpTarget(
    const TdpTriple& base,
    const std::vector<GameProfile>& profiles,
    std::optional<std::size_t> activeProfile);

bool SameTargetIdentity(const TdpTarget& left, const TdpTarget& right) noexcept;
bool TargetValueChanged(const TdpTarget& applied, const TdpTarget& desired) noexcept;

} // namespace LegionGoCore

// Short namespace spelling for clients that prefer it.
namespace legiongo = LegionGoCore;
