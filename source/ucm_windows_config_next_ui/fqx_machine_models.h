#pragma once

#include <array>
#include <cstdint>

namespace ucm::ethercat {

struct MachineModelDefinition {
    std::uint16_t code;
    const char *name;
};

inline constexpr std::array<MachineModelDefinition, 29> kFqxMachineModels {{
    {0x0001U, "LN500"},
    {0x0002U, "LN700"},
    {0x0003U, "LN900"},
    {0x0004U, "LN1100"},
    {0x0005U, "LN1300"},
    {0x0006U, "LN1500"},
    {0x0007U, "LN1700"},
    {0x0008U, "LN1900"},
    {0x0009U, "LN2100"},
    {0x000aU, "LN2400"},
    {0x000bU, "LN2800"},
    {0x000cU, "LN3300"},
    {0x000dU, "LN4000"},
    {0x0021U, "GW1200R"},
    {0x0022U, "GW1400R"},
    {0x0023U, "GW1600R"},
    {0x0024U, "GW1600RP"},
    {0x0025U, "GW1850R"},
    {0x0026U, "GW2200R"},
    {0x0027U, "GW2400R"},
    {0x0028U, "GW3000R"},
    {0x0029U, "GW3300R"},
    {0x0041U, "HB550"},
    {0x0042U, "HB900"},
    {0x0043U, "HB1200"},
    {0x0044U, "HB1400"},
    {0x0045U, "HB1900"},
    {0x0046U, "HB2500"},
    {0x0047U, "HB3500"},
}};

constexpr bool isKnownFqxMachineModel(std::uint16_t code)
{
    for (const auto &model : kFqxMachineModels) {
        if (model.code == code) return true;
    }
    return false;
}

} // namespace ucm::ethercat
