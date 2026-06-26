module;

#include <includes.hpp>

#include <algorithm>
#include <random>
#include <stdexcept>

export module game.world.terrain;

import misc.ptr;
import misc.number;

export namespace craftbuild {
    struct WorldGenerationContext {
        int32 min_y = 0;
        int32 height = 255;

        int32 top_y() const;
    };

    class RandomSource {
    private:
        std::mt19937 generator;

    public:
        explicit RandomSource(uint32 seed) : generator(seed) {}

        int32 next_int(int32 bound);
        int32 next_int(int32 min_inclusive, int32 max_inclusive);
    };

    enum class VerticalAnchorType { ABSOLUTE, ABOVE_BOTTOM, BELOW_TOP, };

    struct VerticalAnchor {
        VerticalAnchorType type = VerticalAnchorType::ABSOLUTE;
        int32 value = 0;

        static VerticalAnchor absolute(int32 y);
        static VerticalAnchor above_bottom(int32 offset);
        static VerticalAnchor below_top(int32 offset);

        int32 resolve_y(const WorldGenerationContext& context) const;
    };

    enum class HeightProviderType { CONSTANT, UNIFORM, BIASED_TO_BOTTOM, VERY_BIASED_TO_BOTTOM, TRAPEZOID, WEIGHTED_LIST };

    class HeightProvider {
    public:
        virtual ~HeightProvider();

        virtual int32 sample(RandomSource& random, const WorldGenerationContext& context) const = 0;
        virtual HeightProviderType get_type() const = 0;
    };

    using HeightProviderPtr = Ptr<const HeightProvider>;

    class ConstantHeight final : public HeightProvider {
    private:
        VerticalAnchor value;

    public:
        explicit ConstantHeight(VerticalAnchor value);

        static HeightProviderPtr of(VerticalAnchor value);

        const VerticalAnchor& get_value() const;
        int32 sample(RandomSource&, const WorldGenerationContext& context) const override;
        HeightProviderType get_type() const override;
    };

    class UniformHeight final : public HeightProvider {
    private:
        VerticalAnchor min_inclusive;
        VerticalAnchor max_inclusive;

    public:
        UniformHeight(VerticalAnchor min_inclusive, VerticalAnchor max_inclusive);

        static HeightProviderPtr of(VerticalAnchor min_inclusive, VerticalAnchor max_inclusive);

        int32 sample(RandomSource& random, const WorldGenerationContext& context) const override;
        HeightProviderType get_type() const override;
    };

    class BiasedToBottomHeight final : public HeightProvider {
    private:
        VerticalAnchor min_inclusive;
        VerticalAnchor max_inclusive;
        int32 inner = 1;

    public:
        BiasedToBottomHeight(VerticalAnchor min_inclusive, VerticalAnchor max_inclusive, int32 inner = 1);

        static HeightProviderPtr of(VerticalAnchor min_inclusive, VerticalAnchor max_inclusive, int32 inner = 1);

        int32 sample(RandomSource& random, const WorldGenerationContext& context) const override;
        HeightProviderType get_type() const override;
    };

    class VeryBiasedToBottomHeight final : public HeightProvider {
    private:
        VerticalAnchor min_inclusive;
        VerticalAnchor max_inclusive;
        int32 inner = 1;

    public:
        VeryBiasedToBottomHeight(VerticalAnchor min_inclusive, VerticalAnchor max_inclusive, int32 inner = 1);

        static HeightProviderPtr of(VerticalAnchor min_inclusive, VerticalAnchor max_inclusive, int32 inner = 1);

        int32 sample(RandomSource& random, const WorldGenerationContext& context) const override;
        HeightProviderType get_type() const override;
    };

    class TrapezoidHeight final : public HeightProvider {
    private:
        VerticalAnchor min_inclusive;
        VerticalAnchor max_inclusive;
        int32 plateau = 0;

    public:
        TrapezoidHeight(VerticalAnchor min_inclusive, VerticalAnchor max_inclusive, int32 plateau = 0);

        static HeightProviderPtr of(VerticalAnchor min_inclusive, VerticalAnchor max_inclusive, int32 plateau = 0);

        int32 sample(RandomSource& random, const WorldGenerationContext& context) const override;
        HeightProviderType get_type() const override;
    };

    class WeightedListHeight final : public HeightProvider {
    public:
        struct Entry {
            HeightProviderPtr provider;
            int32 weight = 1;
        };

    private:
        std::vector<Entry> distribution;
        int32 total_weight = 0;

    public:
        explicit WeightedListHeight(std::vector<Entry> distribution);

        static HeightProviderPtr of(std::vector<Entry> distribution);

        int32 sample(RandomSource& random, const WorldGenerationContext& context) const override;
        HeightProviderType get_type() const override;
    };
}
