// Presentation only. A missing/older label table keeps the existing English
// text; it cannot alter installation phases, restart actions or permissions.
static inline NSString *VIMProgressDisplayLabel(NSDictionary *labels,NSString *english) {
    id value=labels[english];
    return [value isKindOfClass:NSString.class]&&[value length]&&[value length]<=2048?value:english;
}
