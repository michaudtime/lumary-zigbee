// Mirrors the shape of the real Enum expose closely enough to assert on: name,
// access and values are what Z2M's Home Assistant discovery actually reads.
class Enum {
    constructor(name, access, values) {
        this.type = 'enum';
        this.name = name;
        this.property = name;
        this.access = access;
        this.values = values;
    }
    withDescription(description) { this.description = description; return this; }
    withCategory(category) { this.category = category; return this; }
    withLabel(label) { this.label = label; return this; }
}

export const presets = {enum: (name, access, values) => new Enum(name, access, values)};
export const access = {STATE: 0b001, SET: 0b010, ALL: 0b111};
