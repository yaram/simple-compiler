#pragma once

enum struct RegisterSize {
    Size8,
    Size16,
    Size32,
    Size64
};

struct ArchitectureInfo {
    RegisterSize address_size;

    RegisterSize default_size;
};