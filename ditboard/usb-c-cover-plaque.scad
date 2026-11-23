// USB-C cover plaque
$fn = 64;

// Plaque
plate_l = 35;
plate_w = 20;
plate_t = 2;
plate_r = 0.5;                      // corner radius of the plaque (matches the opening)

// USB-C opening (plug 12.3 x 6.7 mm + clearance)
plug_l    = 12.3;
plug_w    = 6.7;
clearance = 0.25;                   // per side
hole_l    = plug_l + 2*clearance;   // 12.8
hole_w    = plug_w + 2*clearance;   // 7.2
hole_r    = 0.5;                    // corner radius of the opening

// Ridge around the opening
ridge_h = 2;
ridge_t = 1.5;                      // wall thickness of the ridge
ridge_r = hole_r + ridge_t;         // outer radius, concentric with the opening

// Screw holes
screw_d      = 2;
screw_offset = 13;                  // distance from center along the long axis

// Centered rounded rectangle, extruded from z=0 to z=h
module rrect(l, w, r, h) {
    hull()
        for (x = [-1, 1], y = [-1, 1])
            translate([x*(l/2 - r), y*(w/2 - r), 0])
                cylinder(r = r, h = h);
}

difference() {
    union() {
        // Base plate
        rrect(plate_l, plate_w, plate_r, plate_t);

        // Ridge (rises 2 mm above the plate)
        translate([0, 0, plate_t])
            rrect(hole_l + 2*ridge_t, hole_w + 2*ridge_t, ridge_r, ridge_h);
    }

    // USB-C opening, through plate and ridge
    translate([0, 0, -1])
        rrect(hole_l, hole_w, hole_r, plate_t + ridge_h + 2);

    // Screw holes
    for (x = [-screw_offset, screw_offset])
        translate([x, 0, -1])
            cylinder(d = screw_d, h = plate_t + 2);
}