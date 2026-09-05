module game.world.terrain;

namespace craftbuild {
    int32 WorldGenerationContext::top_y() const { return min_y + height - 1; }

    int32 RandomSource::next_int(int32 bound) {
        if (bound <= 0) throw std::invalid_argument("RandomSource::next_int bound must be positive");
        std::uniform_int_distribution<int32> distribution(0, bound - 1);
        return distribution(generator);
    }

    int32 RandomSource::next_int(int32 min_inclusive, int32 max_inclusive) {
        if (min_inclusive > max_inclusive) return min_inclusive;
        std::uniform_int_distribution<int32> distribution(min_inclusive, max_inclusive);
        return distribution(generator);
    }

    VerticalAnchor VerticalAnchor::absolute(int32 y) { return { VerticalAnchorType::ABSOLUTE, y }; }
    VerticalAnchor VerticalAnchor::above_bottom(int32 offset) { return { VerticalAnchorType::ABOVE_BOTTOM, offset }; }
    VerticalAnchor VerticalAnchor::below_top(int32 offset) { return { VerticalAnchorType::BELOW_TOP, offset }; }

    int32 VerticalAnchor::resolve_y(WorldGenerationContext const& context) const {
        switch (type) {
        case VerticalAnchorType::ABSOLUTE:     return value;
        case VerticalAnchorType::ABOVE_BOTTOM: return context.min_y + value;
        case VerticalAnchorType::BELOW_TOP:    return context.top_y() - value;
        }

        return value;
    }

    HeightProvider::~HeightProvider() = default;

    ConstantHeight::ConstantHeight(VerticalAnchor value) : value(value) {}

    HeightProviderPtr ConstantHeight::of(VerticalAnchor value) { return new Obj<ConstantHeight>(value); }
    VerticalAnchor const& ConstantHeight::get_value() const { return value; }
    int32 ConstantHeight::sample(RandomSource&, WorldGenerationContext const& context) const { return value.resolve_y(context); }
    HeightProviderType ConstantHeight::get_type() const { return HeightProviderType::CONSTANT; }

    UniformHeight::UniformHeight(VerticalAnchor min_inclusive, VerticalAnchor max_inclusive) : min_inclusive(min_inclusive), max_inclusive(max_inclusive) {}

    HeightProviderPtr UniformHeight::of(VerticalAnchor min_inclusive, VerticalAnchor max_inclusive) { return new Obj<UniformHeight>(min_inclusive, max_inclusive); }
    
    int32 UniformHeight::sample(RandomSource& random, WorldGenerationContext const& context) const {
        const int32 min_y = min_inclusive.resolve_y(context);
        const int32 max_y = max_inclusive.resolve_y(context);
        if (min_y > max_y) return min_y;
        return random.next_int(min_y, max_y);
    }

    HeightProviderType UniformHeight::get_type() const { return HeightProviderType::UNIFORM; }

    BiasedToBottomHeight::BiasedToBottomHeight(VerticalAnchor min_inclusive, VerticalAnchor max_inclusive, int32 inner) : min_inclusive(min_inclusive), max_inclusive(max_inclusive), inner(std::max<int32>(1, inner)) {}

    HeightProviderPtr BiasedToBottomHeight::of(VerticalAnchor min_inclusive, VerticalAnchor max_inclusive, int32 inner) { return new Obj<BiasedToBottomHeight>(min_inclusive, max_inclusive, inner); }

    int32 BiasedToBottomHeight::sample(RandomSource& random, WorldGenerationContext const& context) const {
        const int32 min_y = min_inclusive.resolve_y(context);
        const int32 max_y = max_inclusive.resolve_y(context);
        const int32 bound = max_y - min_y - inner + 1;
        if (bound <= 0) return min_y;

        const int32 offset = random.next_int(bound);
        return random.next_int(offset + inner) + min_y;
    }

    HeightProviderType BiasedToBottomHeight::get_type() const { return HeightProviderType::BIASED_TO_BOTTOM; }

    VeryBiasedToBottomHeight::VeryBiasedToBottomHeight(VerticalAnchor min_inclusive, VerticalAnchor max_inclusive, int32 inner) : min_inclusive(min_inclusive), max_inclusive(max_inclusive), inner(std::max<int32>(1, inner)) {}
    
    HeightProviderPtr VeryBiasedToBottomHeight::of(VerticalAnchor min_inclusive, VerticalAnchor max_inclusive, int32 inner) { return new Obj<VeryBiasedToBottomHeight>(min_inclusive, max_inclusive, inner); }
    
    int32 VeryBiasedToBottomHeight::sample(RandomSource& random, WorldGenerationContext const& context) const {
        const int32 min_y = min_inclusive.resolve_y(context);
        const int32 max_y = max_inclusive.resolve_y(context);
        if (max_y - min_y - inner + 1 <= 0) return min_y;

        const int32 first = random.next_int(min_y + inner, max_y);
        const int32 second = random.next_int(min_y, first - 1);
        return random.next_int(min_y, second - 1 + inner);
    }

    HeightProviderType VeryBiasedToBottomHeight::get_type() const { return HeightProviderType::VERY_BIASED_TO_BOTTOM; }

    TrapezoidHeight::TrapezoidHeight(VerticalAnchor min_inclusive, VerticalAnchor max_inclusive, int32 plateau) : min_inclusive(min_inclusive), max_inclusive(max_inclusive), plateau(plateau) {}

    HeightProviderPtr TrapezoidHeight::of(VerticalAnchor min_inclusive, VerticalAnchor max_inclusive, int32 plateau) { return new Obj<TrapezoidHeight>(min_inclusive, max_inclusive, plateau); }

    int32 TrapezoidHeight::sample(RandomSource& random, WorldGenerationContext const& context) const {
        const int32 min_y = min_inclusive.resolve_y(context);
        const int32 max_y = max_inclusive.resolve_y(context);
        if (min_y > max_y) return min_y;

        const int32 range = max_y - min_y;
        if (plateau >= range) return random.next_int(min_y, max_y);

        const int32 first_range = (range - plateau) / 2;
        const int32 second_range = range - first_range;
        return min_y + random.next_int(0, second_range) + random.next_int(0, first_range);
    }

    HeightProviderType TrapezoidHeight::get_type() const { return HeightProviderType::TRAPEZOID; }

    WeightedListHeight::WeightedListHeight(List<Entry> distribution) : distribution(std::move(distribution)) {
        for (Entry const& entry : this->distribution) {
            if (entry.provider and entry.weight > 0) total_weight += entry.weight;
        }
    }

    HeightProviderPtr WeightedListHeight::of(List<Entry> distribution) { return new Obj<WeightedListHeight>(std::move(distribution)); }

    int32 WeightedListHeight::sample(RandomSource& random, WorldGenerationContext const& context) const {
        if (total_weight <= 0) return 0;

        int32 chosen = random.next_int(total_weight);
        for (Entry const& entry : distribution) {
            if (not entry.provider or entry.weight <= 0) continue;
            if (chosen < entry.weight) return entry.provider.value().sample(random, context);
            chosen -= entry.weight;
        }

        return 0;
    }

    HeightProviderType WeightedListHeight::get_type() const { return HeightProviderType::WEIGHTED_LIST; }
}