//Symbols TempleOS creates at boot (StartOS.HC, DefinePrint in GrGlbls.HC).
public extern I8i Option(I64i num,I8i val);
#define TEXT_ROWS 60
#define TEXT_COLS 80
#define ST_BIBLE_LINES 100110
extern I64i tos_fixed_area; //C runtime: host address of the CSysFixedArea
#define SYS_FIXED_AREA tos_fixed_area
