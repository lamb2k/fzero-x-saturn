/* Original test circuit. No Nintendo course or machine data. MIT. */
#define CP(X,Y,Z,B) { .position = {(X)*65536,(Y)*65536,(Z)*65536}, \
    .radius_left = 18*65536, .radius_right = 18*65536, \
    .bank = (B), .tension = SH2Z_HALF }
static const fzx_course_t fzx_demo_course = {
    .count = 8,
    .points = {
        CP(-180, 0,-280, 0), CP(140, 6,-300, 728),
        CP(280,20,-140,1456), CP(300, 0,160, 0),
        CP(120,-4, 300, 0), CP(-160,0,290, 910),
        CP(-310,16,100,1820), CP(-280,30,-100,910)
    }
};
#undef CP
