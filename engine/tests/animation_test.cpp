import roboslop.animation.clip;
import roboslop.animation.skeleton;

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>

TEST_CASE("sampleClip leaves bones at identity for an empty clip", "[animation]") {
    roboslop::AnimationClip clip;
    clip.tracks.resize(1); // one bone, no keyframes
    std::array<roboslop::BoneTransform, 1> out{};
    roboslop::sampleClip(clip, 0.5, out);
    REQUIRE(out[0].position.x == Catch::Approx(0.0F));
    REQUIRE(out[0].position.y == Catch::Approx(0.0F));
    REQUIRE(out[0].position.z == Catch::Approx(0.0F));
}

TEST_CASE("sampleClip holds a single position keyframe constant", "[animation]") {
    roboslop::AnimationClip clip;
    clip.tracks.resize(1);
    clip.tracks[0].positionTimes = {0.0};
    clip.tracks[0].positions = {{1.0F, 2.0F, 3.0F}};
    clip.durationSec = 1.0;

    std::array<roboslop::BoneTransform, 1> out{};

    roboslop::sampleClip(clip, 0.0, out);
    REQUIRE(out[0].position.x == Catch::Approx(1.0F));
    REQUIRE(out[0].position.y == Catch::Approx(2.0F));
    REQUIRE(out[0].position.z == Catch::Approx(3.0F));

    roboslop::sampleClip(clip, 0.5, out);
    REQUIRE(out[0].position.x == Catch::Approx(1.0F));
}

TEST_CASE("sampleClip linearly interpolates between two position keyframes", "[animation]") {
    roboslop::AnimationClip clip;
    clip.tracks.resize(1);
    clip.tracks[0].positionTimes = {0.0, 1.0};
    clip.tracks[0].positions = {{0.0F, 0.0F, 0.0F}, {10.0F, 20.0F, 30.0F}};
    clip.durationSec = 1.0;

    std::array<roboslop::BoneTransform, 1> out{};
    roboslop::sampleClip(clip, 0.5, out);

    REQUIRE(out[0].position.x == Catch::Approx(5.0F));
    REQUIRE(out[0].position.y == Catch::Approx(10.0F));
    REQUIRE(out[0].position.z == Catch::Approx(15.0F));
}

TEST_CASE("sampleClip clamps past the last keyframe", "[animation]") {
    roboslop::AnimationClip clip;
    clip.tracks.resize(1);
    clip.tracks[0].positionTimes = {0.0, 1.0};
    clip.tracks[0].positions = {{0.0F, 0.0F, 0.0F}, {10.0F, 0.0F, 0.0F}};

    std::array<roboslop::BoneTransform, 1> out{};
    roboslop::sampleClip(clip, 5.0, out);
    REQUIRE(out[0].position.x == Catch::Approx(10.0F));
}

TEST_CASE("toMatrix(BoneTransform) produces identity for default state", "[animation]") {
    const roboslop::BoneTransform bt{};
    const auto m = roboslop::toMatrix(bt);
    REQUIRE(m[0][0] == Catch::Approx(1.0F));
    REQUIRE(m[1][1] == Catch::Approx(1.0F));
    REQUIRE(m[2][2] == Catch::Approx(1.0F));
    REQUIRE(m[3][3] == Catch::Approx(1.0F));
    REQUIRE(m[3][0] == Catch::Approx(0.0F));
}
