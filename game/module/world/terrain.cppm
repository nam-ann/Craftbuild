export module game.world.terrain;

import std;

import misc.ptr;
import misc.list;
import misc.number;

export namespace craftbuild {
    struct WorldGenerationContext final {
        i32 min_y = 0;
        i32 height = 255;

        i32 top_y() const;
    };

    class RandomSource final {
    private:
        std::mt19937 generator;

    public:
        explicit RandomSource(u32 seed) : generator(seed) {}

        i32 next_int(i32 bound);
        i32 next_int(i32 min_inclusive, i32 max_inclusive);
    };

    enum class VerticalAnchorType { ABSOLUTE, ABOVE_BOTTOM, BELOW_TOP, };

    struct VerticalAnchor final {
        VerticalAnchorType type = VerticalAnchorType::ABSOLUTE;
        i32 value = 0;

        static VerticalAnchor absolute(i32 y);
        static VerticalAnchor above_bottom(i32 offset);
        static VerticalAnchor below_top(i32 offset);

        i32 resolve_y(WorldGenerationContext const& context) const;
    };

    enum class HeightProviderType { CONSTANT, UNIFORM, BIASED_TO_BOTTOM, VERY_BIASED_TO_BOTTOM, TRAPEZOID, WEIGHTED_LIST };

    class HeightProvider {
    public:
        virtual ~HeightProvider();

        virtual i32 sample(RandomSource& random, WorldGenerationContext const& context) const = 0;
        virtual HeightProviderType get_type() const = 0;
    };

    using HeightProviderPtr = Ptr<const HeightProvider>;

    class ConstantHeight final : public HeightProvider {
    private:
        VerticalAnchor value;

    public:
        explicit ConstantHeight(VerticalAnchor value);

        static HeightProviderPtr of(VerticalAnchor value);

        VerticalAnchor const& get_value() const;
        i32 sample(RandomSource&, WorldGenerationContext const& context) const override final;
        HeightProviderType get_type() const override final;
    };

    class UniformHeight final : public HeightProvider {
    private:
        VerticalAnchor min_inclusive;
        VerticalAnchor max_inclusive;

    public:
        UniformHeight(VerticalAnchor min_inclusive, VerticalAnchor max_inclusive);

        static HeightProviderPtr of(VerticalAnchor min_inclusive, VerticalAnchor max_inclusive);

        i32 sample(RandomSource& random, WorldGenerationContext const& context) const override final;
        HeightProviderType get_type() const override final;
    };

    class BiasedToBottomHeight final : public HeightProvider {
    private:
        VerticalAnchor min_inclusive;
        VerticalAnchor max_inclusive;
        i32 inner = 1;

    public:
        BiasedToBottomHeight(VerticalAnchor min_inclusive, VerticalAnchor max_inclusive, i32 inner = 1);

        static HeightProviderPtr of(VerticalAnchor min_inclusive, VerticalAnchor max_inclusive, i32 inner = 1);

        i32 sample(RandomSource& random, WorldGenerationContext const& context) const override final;
        HeightProviderType get_type() const override final;
    };

    class VeryBiasedToBottomHeight final : public HeightProvider {
    private:
        VerticalAnchor min_inclusive;
        VerticalAnchor max_inclusive;
        i32 inner = 1;

    public:
        VeryBiasedToBottomHeight(VerticalAnchor min_inclusive, VerticalAnchor max_inclusive, i32 inner = 1);

        static HeightProviderPtr of(VerticalAnchor min_inclusive, VerticalAnchor max_inclusive, i32 inner = 1);

        i32 sample(RandomSource& random, WorldGenerationContext const& context) const override final;
        HeightProviderType get_type() const override final;
    };

    class TrapezoidHeight final : public HeightProvider {
    private:
        VerticalAnchor min_inclusive;
        VerticalAnchor max_inclusive;
        i32 plateau = 0;

    public:
        TrapezoidHeight(VerticalAnchor min_inclusive, VerticalAnchor max_inclusive, i32 plateau = 0);

        static HeightProviderPtr of(VerticalAnchor min_inclusive, VerticalAnchor max_inclusive, i32 plateau = 0);

        i32 sample(RandomSource& random, WorldGenerationContext const& context) const override final;
        HeightProviderType get_type() const override final;
    };

    class WeightedListHeight final : public HeightProvider {
    public:
        struct Entry {
            HeightProviderPtr provider;
            i32 weight = 1;
        };

    private:
        List<Entry> distribution;
        i32 total_weight = 0;

    public:
        explicit WeightedListHeight(List<Entry> distribution);

        static HeightProviderPtr of(List<Entry> distribution);

        i32 sample(RandomSource& random, WorldGenerationContext const& context) const override final;
        HeightProviderType get_type() const override final;
    };
}
