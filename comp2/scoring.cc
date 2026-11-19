#include "scoring.hpp"


static constexpr int YEAR_DIFF_MAX = 2; // max allowed difference in academic year

namespace {
    /// clamp_min(val, minval):
    ///     Set `val` to be at least `minval`.

    static inline float clamp_min(float val, float minval) noexcept {
        return (val < minval) ? minval : val;
    }

    /// normal_cdf(x):
    ///     Compute the CDF of the standard normal distribution at `x`.

    static inline float normal_cdf(float x) noexcept {
        static constexpr float INV_SQRT2 = 0.70710678118f; // 1 / sqrt(2)
        return 0.5f * (1.0f + std::erf(x * INV_SQRT2));
    }

    /// to_upper(sv)::
    ///     Convert a string_view to uppercase.

    static inline std::string to_upper(std::string_view sv) {
        std::string out(sv);
        std::transform(out.begin(), out.end(), out.begin(),
                       [](unsigned char c) { return std::toupper(c); });
        return out;
    }
}  

/// get_match_type(u1, u2):
///     Determine the MatchType between users `u1` and `u2`.

MatchType get_match_type(const User& u1, const User& u2) {
    const bool love = u1.loveprefs.count(u2.gender) &&
                      u2.loveprefs.count(u1.gender);

    const bool friendship = u1.friendprefs.count(u2.gender) &&
                            u2.friendprefs.count(u1.gender);

    return love       ? MatchType::LOVE 
         : friendship ? MatchType::FRIENDSHIP 
         : MatchType::NEITHER;
}

/// get_crush_type(u1, u2):
///     Determine the Crush status between users `u1` and `u2`.

Crush get_crush_type(const User& u1, const User& u2) {
    return (u1.crush == u2.id && u2.crush == u1.id) ? Crush::MUTUAL
         : (u1.crush == u2.id || u2.crush == u1.id) ? Crush::ONE_WAY
         :                                            Crush::NO_CRUSH;
}

/// calculate_rice_purity(rps1, rps2):
///    Calculate the rice purity score compatibility between two users.

float calculate_rice_purity(float rps1, float rps2) noexcept {
    if (rps1 < 0.0f || rps2 < 0.0f) {
        return 1.0f;
    }

    const float alpha = 1.0f / 1000.0f;
    const float beta  = std::max(rps1, rps2) / 100.0f;
    const float diff  = std::fabs(rps1 - rps2);

    const float x = std::pow(diff, 2.4f) * alpha;
    float score   = 1.3f - std::pow(x, beta);
    score = clamp_min(score, 0.0f);

    return ((score + 2.0f) / 3.3f * 1.4f + 0.6f) * 0.75f;
}

/// is_cheating(u1, u2, nquestions, college_dists):
///    Determine if users `u1` and `u2` are likely "cheating", i.e., have 
///    suspiciously similar answers.

bool is_cheating(const User& u1, const User& u2, std::size_t nquestions,
                 const std::unordered_map<std::string, College>& college_dists) {
    if (u1.college != u2.college) {
        return false;
    }

    const auto it = college_dists.find(u1.college);
    if (it == college_dists.end()) {
        return false;
    }

    const College& college = it->second;

    std::size_t matches = 0;
    for (std::size_t i = 0; i < nquestions && i < u1.answers.size() && i < u2.answers.size(); ++i) {
        if (u1.answers[i] == u2.answers[i]) {
            matches += (u1.answers[i] == u2.answers[i]); // bool evaluates to 1 if true
        }
    }

    const bool mutual = (get_crush_type(u1, u2) == Crush::MUTUAL);
    const float threshold = mutual ? 6.5f : 8.5f;
    return (static_cast<float>(matches) - college.exp_sim) > threshold;
}

/// ================================
/// TODO
/// ================================ 

/// check_compatibility(u1, u2):
///     Check if users `u1` and `u2` are compatible matches based on various
///     criteria specified in the README.

namespace {
    /// age_ok(chooser, other):
    ///     True if `other`'s age falls within `chooser`'s age preferences.
    ///     A missing min or max means "no limit" on that side.

    static inline bool age_ok(const User& chooser, const User& other) noexcept {
        if (chooser.min_compatible_age && other.age < *chooser.min_compatible_age) {
            return false;
        }
        if (chooser.max_compatible_age && other.age > *chooser.max_compatible_age) {
            return false;
        }
        return true;
    }

    /// blocks_house(blocker, other):
    ///     True if `blocker` has blocked the house that `other` lives in.

    static inline bool blocks_house(const User& blocker, const User& other) {
        return std::find(blocker.blocked_houses.begin(),
                         blocker.blocked_houses.end(),
                         other.house) != blocker.blocked_houses.end();
    }
}

bool check_compatibility(const User& u1, const User& u2) {
    // 1. Users can't match with themselves.
    if (u1.id == u2.id) {
        return false;
    }

    // 2. Cross-school matching is disabled: colleges must be the same.
    if (u1.college != u2.college) {
        return false;
    }

    // 3. Both must want the same kind of match (love or friendship) with
    //    each other. NEITHER means their preferences don't line up.
    if (get_match_type(u1, u2) == MatchType::NEITHER) {
        return false;
    }

    // 4. Mutual crushes are always compatible: skip every remaining rule.
    if (get_crush_type(u1, u2) == Crush::MUTUAL) {
        return true;
    }

    // 5. Class years can be at most YEAR_DIFF_MAX apart.
    if (std::abs(u1.year - u2.year) > YEAR_DIFF_MAX) {
        return false;
    }

    // 6. Age preferences must hold in both directions.
    if (!age_ok(u1, u2) || !age_ok(u2, u1)) {
        return false;
    }

    // 7. If either user opted out of same-house matches, they can't share one.
    //    (An empty house means "unknown", so it never counts as the same house.)
    if ((u1.no_house_matches || u2.no_house_matches) &&
        !u1.house.empty() && u1.house == u2.house) {
        return false;
    }

    // 8. Neither user may live in a house the other has blocked.
    if (blocks_house(u1, u2) || blocks_house(u2, u1)) {
        return false;
    }

    return true;
}

/// bonus(u1, u2):
///    Our new feature: "seriousness alignment", plus credit for one-way crushes.
///
///    - Seriousness (0..3) says how serious someone is about their match
///      (e.g. 0 = just for fun, 3 = looking for something real). Two people
///      with similar intentions are a better match, so we give up to
///      SERIOUSNESS_WEIGHT points, decreasing linearly with the gap:
///          gap 0 -> full points, gap 3 -> 0 points.
///    - A one-way crush (and they are otherwise compatible) earns a small
///      extra ONE_WAY_CRUSH_BONUS, since mutual crushes are already handled
///      by check_compatibility.

float bonus(const User& u1, const User& u2) {
    static constexpr float SERIOUSNESS_WEIGHT  = 0.25f;
    static constexpr float SERIOUSNESS_RANGE   = 3.0f;  // scale is 0..3
    static constexpr float ONE_WAY_CRUSH_BONUS = 0.15f;

    float points = 0.0f;

    // Seriousness alignment (clamp the gap in case of out-of-range input).
    const float gap = std::min(
        static_cast<float>(std::abs(u1.seriousness - u2.seriousness)),
        SERIOUSNESS_RANGE);
    points += SERIOUSNESS_WEIGHT * (1.0f - gap / SERIOUSNESS_RANGE);

    // One-way crush bonus.
    if (get_crush_type(u1, u2) == Crush::ONE_WAY && check_compatibility(u1, u2)) {
        points += ONE_WAY_CRUSH_BONUS;
    }

    return points;
}

/// ================================
/// END OF TODO
/// ================================


/// calculate_score(u1, u2, weight, num_questions, college_dists, bio_score,
///                 short_answer_score, spotify_score):
///     Calculate the compatibility score between two users.

float calculate_score(
    const User& u1,
    const User& u2,
    float weight,
    std::size_t num_questions,
    const std::unordered_map<std::string, College>& college_dists,
    float bio_score,
    float short_answer_score,
    float spotify_score
) {
    if (is_cheating(u1, u2, num_questions, college_dists)) {
        return -1.0f;
    }

    if (u1.college != u2.college) {
        return -1.0f;
    }

    const auto it = college_dists.find(u1.college);
    if (it == college_dists.end()) {
        return -1.0f; // unknown college
    }

    const College& college = it->second;
    const std::size_t nquestions = std::min({
        num_questions, college.num_questions, 
        u1.answers.size(), u2.answers.size()
    });
    const std::size_t num_options = college.num_options;

    float ml_score   = 0.0f;
    float prop_score = 0.0f;

    const float denom_guard = clamp_min(std::sqrt(college.dist_min), 1e-6f);
    const float norm_ref    = 1.0f / denom_guard;

    for (std::size_t i = 0; i < nquestions; ++i) {
        const int a1 = u1.answers[i];
        const int a2 = u2.answers[i];

        if (a1 < 0 || a1 >= static_cast<std::size_t>(num_options) ||
            a2 < 0 || a2 >= static_cast<std::size_t>(num_options)) {
            continue;
        }

        const float p1 = college.dist(i, static_cast<std::size_t>(a1));
        if (a1 == a2) {
            const float imp_wgt = 1.0f / (1.0f + p1);
            prop_score += (imp_wgt / clamp_min(std::sqrt(p1), 1e-6f)) / norm_ref;
            ml_score   += imp_wgt;
        } else {
            const float p2 = college.dist(i, static_cast<std::size_t>(a2));
            const float imp_wgt = 1.0f / (1.0f + std::sqrt(clamp_min(p1 * p2, 0.0f)));
            ml_score += imp_wgt * college.sim(i, static_cast<std::size_t>(a1), static_cast<std::size_t>(a2));
        }
    }

    float sim_score = ml_score + prop_score + bio_score * 0.10f + 
                      short_answer_score * 0.10f + spotify_score * 0.30f;

    // Your bonus will go here!
    sim_score += bonus(u1, u2);

    sim_score = clamp_min(sim_score, 0.001f);
    return weight * sim_score;
}