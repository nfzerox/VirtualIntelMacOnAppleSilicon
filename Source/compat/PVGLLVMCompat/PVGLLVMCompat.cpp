#include <string>

namespace llvm {

struct EVT {
    unsigned char simple_type;
    unsigned char padding[7];
    void *extended_type;

    std::string getEVTString() const;
};

std::string EVT::getEVTString() const
{
    static const char *const names[] = {
        "INVALID", "Other", "i1", "i8", "i16", "i32", "i64", "i128",
        "f16", "f32", "f64", "f80", "f128", "ppcf128"
    };
    if (simple_type < sizeof(names) / sizeof(names[0]))
        return names[simple_type];
    if (simple_type == 108)
        return "x86mmx";
    if (simple_type == 110)
        return "isVoid";
    if (simple_type == 248)
        return "token";
    if (simple_type == 249)
        return "Metadata";
    if (simple_type == 254)
        return "iPTR";
    return extended_type ? "Extended" : "Unknown";
}

} // namespace llvm
