// Guest-side semantic lowering candidate. No host image or math-table changes.
double roundEven(double value) {
    double lower = floor(value);
    double fraction = value - lower;
    double lowerHalf = lower * 0.5;
    if (fraction > 0.5 || (fraction == 0.5 && lowerHalf != floor(lowerHalf)))
        lower += 1.0;
    return lower == 0.0 ? value * 0.0 : lower;
}
dvec2 roundEven(dvec2 value) {
    return dvec2(roundEven(value.x), roundEven(value.y));
}
dvec3 roundEven(dvec3 value) {
    return dvec3(roundEven(value.x), roundEven(value.y), roundEven(value.z));
}
dvec4 roundEven(dvec4 value) {
    return dvec4(roundEven(value.x), roundEven(value.y), roundEven(value.z), roundEven(value.w));
}
