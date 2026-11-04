// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
#pragma once

#include <array>

namespace openrac::frontend::debug_overlay {

enum class PresetSurface { Opaque, Metal, Glass, Water, Ocean, Foliage, DryFur, WetFur };

struct MaterialPreset {
    PresetSurface surface;
    const char* name;
    const char* description;
    double roughness = 0;
    double reflectivity = .04;
    std::array<double, 3> colour{1, 1, 1};  // Metal F0 or glass transmission tint, in linear RGB.
    double transmission = 0;
};

// Roughness is a suggested finish. Metal F0 uses Epic's measured-material reference values.
inline constexpr MaterialPreset kMaterialPresets[] = {
    {PresetSurface::Opaque, "Glossy plastic", "Crisp highlights for buttons and colourful gadget shells.", .22},
    {PresetSurface::Opaque, "Satin plastic", "Soft highlights for robot trim and weapon grips.", .40},
    {PresetSurface::Opaque, "Matte rubber", "Broad highlights for tyres, seals and boot soles.", .78},
    {PresetSurface::Opaque, "Gloss enamel paint", "A smooth painted finish for ship and robot panels.", .24},
    {PresetSurface::Opaque, "Powder-coated paint", "A subdued finish for industrial doors and casings.", .57},
    {PresetSurface::Opaque, "Glazed ceramic", "A smooth glaze for pottery and decorative surfaces.", .18},
    {PresetSurface::Opaque, "Terracotta", "A dry, unglazed finish for clay pots and masonry.", .79},
    {PresetSurface::Opaque, "Dry concrete", "Broad, weak highlights for platforms and bunker walls.", .84},
    {PresetSurface::Opaque, "Polished stone", "A smooth finish for monuments and interior floors.", .29, .045},
    {PresetSurface::Opaque, "Rough sandstone", "A dry, granular finish for cliffs and masonry.", .87},
    {PresetSurface::Opaque, "Sealed wood", "A satin finish for finished timber and props.", .39},
    {PresetSurface::Opaque, "Raw wood", "Broad highlights for crates and unfinished boards.", .73},
    {PresetSurface::Opaque, "Dry sand", "A very matte finish for dry beaches and dunes.", .92},
    {PresetSurface::Opaque, "Worn asphalt", "A coarse finish for roads and landing pads.", .83},
    {PresetSurface::Opaque, "Matte interior paint", "Soft highlights for painted walls and ceilings.", .72},
    {PresetSurface::Opaque, "Painted plaster", "A muted wall finish for interiors such as Novalis.", .78},
    {PresetSurface::Opaque, "Rough stucco", "A coarse finish for exterior walls and columns.", .91},
    {PresetSurface::Opaque, "Fired red brick", "A rough fired-clay finish for masonry and chimneys.", .80},
    {PresetSurface::Opaque, "Chalky mortar", "A very matte finish for seams and exposed mortar.", .94},
    {PresetSurface::Opaque, "Glazed floor tile", "A tight glaze highlight for clean corridors and lobbies.", .20},
    {PresetSurface::Opaque, "Terrazzo", "A lightly polished finish for patterned interior floors.", .34},
    {PresetSurface::Opaque, "Trowelled concrete", "A satin finish for workshops and hangars.", .46},
    {PresetSurface::Opaque, "Vinyl flooring", "A soft sheen for ship interiors and control rooms.", .42},
    {PresetSurface::Opaque, "Honed limestone", "A smooth matte finish for steps and worn monuments.", .57},
    {PresetSurface::Opaque, "Polished granite", "A tight highlight for decorative floors and counters.", .23, .045},
    {PresetSurface::Opaque, "Rough basalt", "A dark-rock finish for volcanic cliffs and rubble.", .89},
    {PresetSurface::Opaque, "Packed dirt", "A broad highlight for trails and exposed soil.", .90},
    {PresetSurface::Opaque, "Damp compacted sand", "A tighter highlight for the swash zone. Tune diffuse brightness separately.", .59},
    {PresetSurface::Opaque, "Coarse gravel", "A very matte finish for road shoulders and riverbeds.", .92},
    {PresetSurface::Opaque, "Compacted snow", "A broad highlight for snow paths and banks. Uses opaque shading.", .84, .035},
    {PresetSurface::Opaque, "Lacquered veneer", "A glossy finish for furniture and decorative panelling.", .24},
    {PresetSurface::Opaque, "Oiled hardwood", "A soft sheen for handrails and worn floors.", .43},
    {PresetSurface::Opaque, "Weathered dock timber", "A rough finish for sun-worn docks and pilings.", .84},
    {PresetSurface::Opaque, "Textured ABS", "A satin moulded finish for gadgets and grips.", .53},
    {PresetSurface::Opaque, "Opaque acrylic", "A smooth finish for coloured buttons and trims.", .21, .039},
    {PresetSurface::Opaque, "Smooth leather", "A soft sheen for seats, straps and gloves.", .46},
    {PresetSurface::Opaque, "Woven canvas", "A matte finish for sacks, awnings and upholstery.", .85, .045},
    {PresetSurface::Opaque, "Oxidised iron crust", "A rough dielectric finish for rusted areas of industrial props.", .88},
    {PresetSurface::Metal, "Satin chrome", "Soft silver reflections for Clank's bare metal panels.", .26, 0, {.550, .556, .554}},
    {PresetSurface::Metal, "Polished chrome", "Tight reflections for the wrench and bright fittings.", .12, 0, {.550, .556, .554}},
    {PresetSurface::Metal, "Machined aluminium", "Bright neutral reflections for ship trim and housings.", .30, 0, {.913, .921, .925}},
    {PresetSurface::Metal, "Bead-blasted aluminium", "Broad neutral reflections for industrial casings.", .56, 0, {.913, .921, .925}},
    {PresetSurface::Metal, "Dark iron", "Broad, darker reflections for railings and machinery.", .52, 0, {.560, .570, .580}},
    {PresetSurface::Metal, "Brushed nickel", "Soft warm reflections for fittings. Uses isotropic highlights.", .36, 0, {.660, .609, .526}},
    {PresetSurface::Metal, "Satin titanium", "Muted warm-grey reflections for panels and barrels.", .33, 0, {.542, .497, .449}},
    {PresetSurface::Metal, "Polished silver", "Bright, near-neutral reflections for trim and bolts.", .17, 0, {.972, .960, .915}},
    {PresetSurface::Metal, "Satin copper", "Warm red-orange reflections for pipes and coils.", .34, 0, {.955, .637, .538}},
    {PresetSurface::Metal, "Polished gold", "Tight golden reflections for trophies and bolts.", .20, 0, {1, .766, .336}},
    {PresetSurface::Metal, "Worn gold", "Broader golden reflections for ornamental trim.", .44, 0, {1, .766, .336}},
    {PresetSurface::Metal, "Cobalt alloy", "Subdued neutral reflections for machine parts.", .39, 0, {.662, .655, .634}},
    {PresetSurface::Glass, "Clear canopy", "High transmission and tight reflections for a clear visor.", .10, .04, {1, 1, 1}, .94},
    {PresetSurface::Glass, "Smoke-tinted canopy", "Lower transmission and a neutral dark tint for a visor.", .16, .04, {.18, .19, .20}, .70},
    {PresetSurface::Glass, "Frosted glass", "Broad reflections and a pale tint. The transmitted scene stays sharp.", .46, .04, {.75, .78, .82}, .55},
    {PresetSurface::Water, "Calm pool", "A smooth local-water finish. Keeps the level's waves and colour.", .10, .020},
    {PresetSurface::Ocean, "Tropical ocean", "A soft FFT ocean highlight. Keeps the level's colour, absorption and waves.", .16, .020},
    {PresetSurface::Foliage, "Waxy tropical leaf", "A satin leaf surface with visible backlighting.", .47, .04, {1, 1, 1}, .45},
    {PresetSurface::Foliage, "Dry leaf", "A rough leaf surface with weaker backlighting.", .72, .04, {1, 1, 1}, .14},
    {PresetSurface::DryFur, "Dry fur", "Directional strand detail and subtle scattering on Ratchet's verified coat."},
    {PresetSurface::WetFur, "Wet fur", "Darkens the verified coat and tightens highlights when wet."},
};
}  // namespace openrac::frontend::debug_overlay
