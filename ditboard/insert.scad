$fn=64;

pin_diameter=2.25;
pin_height=4;
board_width=76;
board_depth=51;
board_height=1.5;

pin();
translate([50,0,0])
pin();
translate([0,40,0])
pin();
translate([50,40,0])
pin();

insert_with_pico();

module vents() {
    
    union(){
        translate([20,5,0])
        {
            translate([-2,0,0])
            cube([4,30,20],center=false);
            cylinder(h=20,r=2, center=false);
        }
        translate([20,35,0])
        cylinder(h=20,r=2, center=false);
    }
    union() {
        translate([30,5,0])
        {
            translate([-2,0,0])
            cube([4,30,20],center=false);
            cylinder(h=20,r=2, center=false);
        }
        translate([30,35,0])
        cylinder(h=20,r=2, center=false);
    }
    union() {
        translate([40,5,0])
        {
            translate([-2,0,0])
            cube([4,30,20],center=false);
            cylinder(h=20,r=2, center=false);
        }
        translate([40,35,0])
        cylinder(h=20,r=2, center=false);
    }

}


module insert_with_pico()
{
    difference() {
        insert();
        group() {
            translate([16,40/2-board_depth/2+(board_depth-47)/2,0])
                pico_pins();
            translate([16,40/2-board_depth/2+(board_depth-47)/2,0])
                board_pins();
            vents();
        }
    }
}

module insert() {
    difference() {
        board();
        group() {
            translate([50/2-board_width/2,40/2-12.5,0])
                cube(25);
            translate([board_width-23,40/2-7.5,0])
                cube([10,15,20]);
        }
    }
}


module board() {
    difference() {
        translate([50/2-board_width/2,40/2-board_depth/2,pin_height])
            cube([board_width, board_depth, board_height]);
        corner_cutout();
    }
}


module pin() {
    cylinder(h=pin_height,r=pin_diameter/2,center=false);    
}

module corner_cutout() {
    translate([-(board_width-50)/2,-(board_depth-40)/2,0])
    corner();

    translate([board_width,0,0])
    translate([-(board_width-50)/2,-(board_depth-40)/2,0])
    corner();

    translate([0,board_depth,0])
    translate([-(board_width-50)/2,-(board_depth-40)/2,0])
    corner();

    translate([board_width,board_depth,0])
    translate([-(board_width-50)/2,-(board_depth-40)/2,0])
    corner();
}

module corner() {
    cylinder(h=20,r=7,center=false);
}

module pico_pins() {
    cylinder(h=20,r=1.25,center=false);
    translate([11.4,0,0])
    cylinder(h=20,r=1.25,center=false);
    translate([11.4,47,0])
    cylinder(h=20,r=1.25,center=false);
    translate([0,47,0])
    cylinder(h=20,r=1.25,center=false);
}

module board_pins() {
    translate([11.4+4.8+3,0.2,0])
        cylinder(h=20,r=1.25,center=false);
    translate([11.4+4.8+3,47-0.4,0])
        cylinder(h=20,r=1.25,center=false);
    translate([11.4+4.8+3+15,(47-0.4)/2,0])
        cylinder(h=20,r=1.25,center=false);


}