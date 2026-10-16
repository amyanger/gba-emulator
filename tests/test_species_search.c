#include "test_harness.h"
#include "game_fixture.h"
#include "game/species_search.h"

static uint8_t s_rom[0x10000];

#define NAMES 0x08000100u
#define NATL 0x08000400u
#define EVOS 0x08001000u
#define HEADERS 0x08002000u
#define FEEBAS 0x08002800u

static const GameProfile k_profile = {
    .name = "test", .rules = &g_emerald_rules, .species_count = 12, .species_names = NAMES,
    .species_to_national = NATL, .evolution_table = EVOS,
    .wild_mon_headers = HEADERS, .wild_header_cap = 8, .wild_feebas = FEEBAS,
};

static GameMem mem(void) {
    GameMem m = { NULL, NULL, s_rom, sizeof(s_rom) };
    return m;
}

static uint8_t* at(uint32_t addr) { return s_rom + (addr - 0x08000000); }

/* Gen 3 bytes: A=0xBB, a=0xD5, space=0x00, '.'=0xAD, '?'=0xAC. Uppercase only here. */
static void put_name(uint16_t sp, const char* s, uint8_t tail) {
    uint8_t* d = at(NAMES + 11u * sp);
    memset(d, 0xFF, 11);
    size_t n = 0;
    for (; s[n]; n++) {
        char c = s[n];
        d[n] = c == ' ' ? 0x00 : c == '.' ? 0xAD : c == '?' ? 0xAC : (uint8_t)(0xBB + (c - 'A'));
    }
    if (tail) d[n] = tail;
}

static void set_nat(uint16_t sp, uint16_t nat) { fixture_put16(at(NATL + 2u * (sp - 1u)), nat); }

static void evo_full(uint16_t from, int slot, uint16_t method, uint16_t param, uint16_t to) {
    uint8_t* e = at(EVOS + (from * 5u + (uint32_t)slot) * 8u);
    fixture_put16(e, method);
    fixture_put16(e + 2, param);
    fixture_put16(e + 4, to);
}

static void evo(uint16_t from, int slot, uint16_t to) { evo_full(from, slot, 4, 0, to); }

static void header(int i, uint8_t group, uint8_t num, uint32_t land_info) {
    uint8_t* h = at(HEADERS + 20u * (uint32_t)i);
    h[0] = group; h[1] = num;
    fixture_put32(h + 4, land_info);
}

static void land_info(uint32_t info, uint32_t mons, uint16_t species) {
    fixture_put32(at(info + 4), mons);
    for (int i = 0; i < 12; i++) {
        uint8_t* m = at(mons + 4u * (uint32_t)i);
        m[0] = 5; m[1] = 6; fixture_put16(m + 2, species);
    }
}

static void build(void) {
    memset(s_rom, 0, sizeof(s_rom));
    put_name(1, "RALTS", 0);   set_nat(1, 280);
    put_name(2, "KIRLIA", 0);  set_nat(2, 281);
    put_name(3, "GARDEVOIR", 0); set_nat(3, 282);
    put_name(4, "MEW", 0);     set_nat(4, 151);
    put_name(5, "MEWTWO", 0);  set_nat(5, 150);
    put_name(6, "NIDORAN", 0xB6); set_nat(6, 29);
    put_name(7, "NIDORAN", 0xB5); set_nat(7, 32);
    put_name(8, "MR. MIME", 0); set_nat(8, 122);
    put_name(9, "?", 0);       set_nat(9, 400);
    put_name(10, "BAYLEEF", 0); set_nat(10, 153);
    put_name(11, "FEEBAS", 0); set_nat(11, 349);
    evo(1, 0, 2);
    evo(2, 0, 3);
    evo(3, 0, 1);              /* cycle back: first writer (species 1 -> 2 is parent[2]) wins */
    /* Headers: 0 = map 1:1 with species 4; 1 = same map, species 5 (ignored);
     * 2 = map 1:2, species 1; 3 = terminator. */
    header(0, 1, 1, 0x08003000);
    header(1, 1, 1, 0x08003100);
    header(2, 1, 2, 0x08003200);
    at(HEADERS + 60)[0] = 0xFF; at(HEADERS + 61)[0] = 0xFF;
    land_info(0x08003000, 0x08003400, 4);
    land_info(0x08003100, 0x08003500, 5);
    land_info(0x08003200, 0x08003600, 1);
    fixture_put16(at(FEEBAS + 2), 11);
}

static SpeciesIndex ix;

TEST(index_builds_names_and_gender) {
    build();
    GameMem m = mem();
    species_index_build(&ix, &k_profile, &m);
    ASSERT_TRUE(ix.ok);
    ASSERT_STR_EQ(ix.name[6], "NIDORAN");
    ASSERT_EQ(ix.gender_mark[6], 1);
    ASSERT_EQ(ix.gender_mark[7], 2);
    ASSERT_STR_EQ(ix.name[10], "BAYLEEF");
    ASSERT_EQ(ix.gender_mark[10], 0);
    ASSERT_STR_EQ(ix.key[8], "mrmime");
    ASSERT_STR_EQ(ix.key[6], "nidoranf");
    ASSERT_STR_EQ(ix.key[7], "nidoranm");
}

TEST(index_skips_placeholders) {
    build();
    GameMem m = mem();
    species_index_build(&ix, &k_profile, &m);
    ASSERT_EQ(ix.count, 10);
    for (int i = 0; i < ix.count; i++) ASSERT_TRUE(ix.order[i] != 9);
    ASSERT_EQ(ix.order[0], 6);   /* national 29 first */
    ASSERT_EQ(ix.national[9], 0);
}

TEST(index_parent_from_8_byte_stride) {
    build();
    GameMem m = mem();
    species_index_build(&ix, &k_profile, &m);
    ASSERT_EQ(ix.parent[2], 1);
    ASSERT_EQ(ix.parent[3], 2);
    ASSERT_EQ(ix.parent[1], 3);
    ASSERT_EQ(ix.parent[4], 0);
}

TEST(index_wild_first_header_per_map) {
    build();
    GameMem m = mem();
    species_index_build(&ix, &k_profile, &m);
    ASSERT_TRUE(species_index_is_wild(&ix, 4));
    ASSERT_TRUE(!species_index_is_wild(&ix, 5));
    ASSERT_TRUE(species_index_is_wild(&ix, 1));
    ASSERT_TRUE(species_index_is_wild(&ix, 11));
    ASSERT_TRUE(!species_index_is_wild(&ix, 2));
}

TEST(query_exact_then_prefix_then_substring) {
    build();
    GameMem m = mem();
    species_index_build(&ix, &k_profile, &m);
    uint16_t out[SPECIES_MAX];
    ASSERT_EQ(species_search_query(&ix, "mew", out), 2);
    ASSERT_EQ(out[0], 4); ASSERT_EQ(out[1], 5);
    ASSERT_EQ(species_search_query(&ix, "ral", out), 1);
    ASSERT_EQ(out[0], 1);
    ASSERT_EQ(species_search_query(&ix, "dor", out), 2);
    ASSERT_EQ(out[0], 6); ASSERT_EQ(out[1], 7);
    ASSERT_EQ(species_search_query(&ix, "nidoran f", out), 1);
    ASSERT_EQ(out[0], 6);
    ASSERT_EQ(species_search_query(&ix, "Mr. Mime", out), 1);
    ASSERT_EQ(out[0], 8);
}

TEST(query_punctuation_only_is_empty) {
    build();
    GameMem m = mem();
    species_index_build(&ix, &k_profile, &m);
    uint16_t out[SPECIES_MAX];
    ASSERT_EQ(species_search_query(&ix, "  .'", out), 10);
}

TEST(index_rejects_bad_tables) {
    build();
    GameMem m = mem();
    GameProfile p = k_profile;
    p.species_count = 500;
    species_index_build(&ix, &p, &m);
    ASSERT_TRUE(!ix.ok);
    p = k_profile;
    p.species_names = 0x08100000;
    species_index_build(&ix, &p, &m);
    ASSERT_TRUE(!ix.ok);
    species_index_build(&ix, NULL, &m);
    ASSERT_TRUE(!ix.ok);
}


/* ---- Detail fixture: map groups, region map names, items and a second header table. ---- */

#define HDR2      0x08004000u
#define MAPGROUPS 0x0800502Cu /* group lists at 0x5000 (8 maps) and 0x5020 (3 maps) end here */
#define MAPHDRS   0x08005100u
#define REGMAP    0x08005800u
#define REGNAMES  0x08006000u
#define TABLES    0x08007400u
#define ITEMS     0x08008000u
#define TGROUP    0x0800B000u /* truncation: one group of 130 maps */
#define TMAPGROUPS (TGROUP + 130u * 4u)
#define TMAPHDRS  0x0800B300u
#define THDR      0x0800C200u
#define TINFO     0x0800CD00u

enum { S_MILOTIC = 12, S_MARILL, S_AZURILL, S_CHIKORITA, S_SNORUNT, S_GLALIE, S_CLAMPERL,
       S_WYNAUT, S_DITTO, S_COUNT };

static const GameProfile k_detail = {
    .name = "detail", .rules = &g_emerald_rules, .species_count = S_COUNT, .species_names = NAMES,
    .species_to_national = NATL, .evolution_table = EVOS,
    .wild_mon_headers = HDR2, .wild_header_cap = 16, .wild_feebas = FEEBAS,
    .map_groups = MAPGROUPS, .map_group_count = 2,
    .region_map_entries = REGMAP, .region_map_count = 140,
    .items = ITEMS, .item_count = 230,
    .feebas_map_group = 0, .feebas_map_num = 4,
    .mirage_map_group = 0, .mirage_map_num = 5, .mirage_mapsec = 71,
    .dive_map = {{0, 6}, {0, 7}}, .dive_surface_mapsec = {39, 41},
    .postgame_map = {{1, 0}, {1, 1}, {1, 0}, {1, 0}, {1, 0}},
    .low_tide_mapsec = 67,
    .incense_baby = {S_AZURILL, S_WYNAUT}, .incense_item = {220, 221},
};

/* Gen 3 text with digits and spaces, 0xFF terminated. */
static void put_text(uint32_t addr, const char* s) {
    uint8_t* d = at(addr);
    size_t n = 0;
    for (; s[n]; n++) {
        char c = s[n];
        d[n] = c == ' ' ? 0x00 : (c >= '0' && c <= '9') ? (uint8_t)(0xA1 + (c - '0'))
                                                       : (uint8_t)(0xBB + (c - 'A'));
    }
    d[n] = 0xFF;
}

static void region_name(uint8_t sec, const char* name) {
    uint32_t str = REGNAMES + 0x20u * sec;
    put_text(str, name);
    fixture_put32(at(REGMAP + 8u * sec + 4), str);
}

static uint32_t s_table_next;

/* A WildPokemonInfo with n slots all `sp` at lo..hi; returns the info pointer. */
static uint32_t table(int n, uint16_t sp, uint8_t lo, uint8_t hi) {
    uint32_t info = s_table_next, mons = info + 8;
    s_table_next += 64;
    fixture_put32(at(info + 4), mons);
    for (int i = 0; i < n; i++) {
        uint8_t* m = at(mons + 4u * (uint32_t)i);
        m[0] = lo; m[1] = hi; fixture_put16(m + 2, sp);
    }
    return info;
}

static void set_slot(uint32_t info, int i, uint16_t sp, uint8_t lo, uint8_t hi) {
    uint8_t* m = at(info + 8 + 4u * (uint32_t)i);
    m[0] = lo; m[1] = hi; fixture_put16(m + 2, sp);
}

static void header2(uint32_t base, int i, uint8_t group, uint8_t num, uint32_t land, uint32_t water,
                    uint32_t rock, uint32_t fish) {
    uint8_t* h = at(base + 20u * (uint32_t)i);
    h[0] = group; h[1] = num;
    fixture_put32(h + 4, land);
    fixture_put32(h + 8, water);
    fixture_put32(h + 12, rock);
    fixture_put32(h + 16, fish);
}

static void build_detail(void) {
    build();
    put_name(S_MILOTIC, "MILOTIC", 0);   put_name(S_MARILL, "MARILL", 0);
    put_name(S_AZURILL, "AZURILL", 0);   put_name(S_CHIKORITA, "CHIKORITA", 0);
    put_name(S_SNORUNT, "SNORUNT", 0);   put_name(S_GLALIE, "GLALIE", 0);
    put_name(S_CLAMPERL, "CLAMPERL", 0); put_name(S_WYNAUT, "WYNAUT", 0);
    put_name(S_DITTO, "DITTO", 0);
    for (uint16_t sp = 12; sp < S_COUNT; sp++) set_nat(sp, (uint16_t)(200 + sp));

    evo_full(1, 0, 4, 20, 2);                  /* RALTS -> KIRLIA Lv 20 */
    evo_full(2, 0, 4, 30, 3);                  /* KIRLIA -> GARDEVOIR Lv 30 */
    evo_full(4, 0, 7, 93, 5);                  /* MEW -> MEWTWO with item 93 */
    evo_full(11, 0, 15, 170, S_MILOTIC);       /* FEEBAS -> MILOTIC by Beauty */
    evo_full(S_AZURILL, 0, 1, 0, S_MARILL);
    evo_full(S_CHIKORITA, 0, 4, 16, 10);       /* CHIKORITA -> BAYLEEF, neither wild */
    evo_full(S_SNORUNT, 0, 4, 42, S_GLALIE);   /* a cycle with no wild member */
    evo_full(S_GLALIE, 0, 4, 43, S_SNORUNT);
    at(FEEBAS)[0] = 20; at(FEEBAS + 1)[0] = 25;

    put_text(ITEMS + 44u * 93, "SUN STONE");
    put_text(ITEMS + 44u * 220, "SEA INCENSE");
    put_text(ITEMS + 44u * 221, "LAX INCENSE");

    /* Map groups: group 0 maps 0..7, group 1 maps 0..2; map header k at MAPHDRS + 0x1C*k. */
    static const uint8_t secs[11] = {10, 10, 67, 11, 12, 13, 50, 50, 20, 20, 20};
    for (uint32_t k = 0; k < 11; k++) {
        uint32_t list = k < 8 ? 0x08005000u + 4u * k : 0x08005020u + 4u * (k - 8);
        fixture_put32(at(list), MAPHDRS + 0x1Cu * k);
        at(MAPHDRS + 0x1Cu * k + 0x14)[0] = secs[k];
    }
    fixture_put32(at(MAPGROUPS), 0x08005000u);
    fixture_put32(at(MAPGROUPS + 4), 0x08005020u);

    for (uint8_t sec = 0; sec < 140; sec++) region_name(sec, "ROUTE");
    region_name(10, "ROUTE 101");  region_name(11, "ROUTE 102");
    region_name(12, "ROUTE 119");  region_name(13, "ROUTE 130");
    region_name(20, "SAFARI ZONE"); region_name(39, "ROUTE 124");
    region_name(41, "ROUTE 126");  region_name(50, "UNDERWATER");
    region_name(67, "SHOAL CAVE"); region_name(71, "MIRAGE ISLAND");

    s_table_next = TABLES;
    uint32_t h0 = table(12, 6, 3, 5);              /* filler: NIDORAN F */
    set_slot(h0, 0, 1, 3, 5); set_slot(h0, 1, 1, 4, 5);       /* RALTS 40% */
    uint32_t h1 = table(12, 6, 7, 8);
    set_slot(h1, 2, 1, 7, 8);                                 /* RALTS 10% */
    uint32_t h4w = table(5, S_MARILL, 20, 30);
    set_slot(h4w, 4, S_CLAMPERL, 20, 20);                     /* CLAMPERL 1% surfing */
    header2(HDR2, 0, 0, 0, h0, 0, 0, 0);
    header2(HDR2, 1, 0, 1, h1, 0, 0, 0);
    header2(HDR2, 2, 0, 0, table(12, 5, 50, 50), 0, 0, 0);   /* same map again: unreachable */
    header2(HDR2, 3, 0, 3, table(12, 4, 5, 5), table(5, S_MARILL, 10, 20),
            table(5, S_CLAMPERL, 15, 15), table(10, S_MARILL, 5, 10));
    header2(HDR2, 4, 0, 4, 0, h4w, 0, table(10, S_MARILL, 25, 30));
    header2(HDR2, 5, 0, 5, table(12, S_WYNAUT, 5, 10), table(5, S_MARILL, 30, 35), 0, 0);
    header2(HDR2, 6, 0, 6, 0, table(5, S_CLAMPERL, 20, 30), 0, 0);
    header2(HDR2, 7, 0, 7, 0, table(5, S_CLAMPERL, 25, 35), 0, 0);
    header2(HDR2, 8, 1, 0, table(12, S_DITTO, 30, 35), 0, 0, 0);
    uint32_t h9 = table(12, 6, 30, 30);
    set_slot(h9, 0, S_DITTO, 36, 38);                         /* DITTO 20% */
    header2(HDR2, 9, 1, 1, h9, 0, 0, 0);
    uint32_t h10 = table(12, 6, 30, 30);
    set_slot(h10, 11, S_DITTO, 10, 10);                       /* DITTO 1% */
    header2(HDR2, 10, 1, 2, h10, 0, 0, 0);
    uint32_t h11 = table(12, 6, 30, 30);
    set_slot(h11, 0, S_DITTO, 20, 20); set_slot(h11, 1, S_DITTO, 21, 22);
    header2(HDR2, 11, 0, 2, h11, 0, 0, 0);
    at(HDR2 + 20u * 12)[0] = 0xFF;
}

static SpeciesDetail det;

static void detail_of(const GameProfile* p, uint16_t sp) {
    GameMem m = mem();
    species_index_build(&ix, p, &m);
    species_search_detail(&ix, p, &m, sp, &det);
}

static void check_row(const SpeciesRow* r, uint8_t method, uint8_t sec, const char* place,
                      uint8_t lo, uint8_t hi, uint8_t pct_lo, uint8_t pct_hi, uint8_t flags) {
    ASSERT_EQ(r->method, method);
    ASSERT_EQ(r->mapsec, sec);
    ASSERT_STR_EQ(r->place, place);
    ASSERT_EQ(r->min_level, lo);
    ASSERT_EQ(r->max_level, hi);
    ASSERT_EQ(r->pct_lo, pct_lo);
    ASSERT_EQ(r->pct_hi, pct_hi);
    ASSERT_EQ(r->flags, flags);
}

TEST(detail_wild_rows_merged) {
    build_detail();
    detail_of(&k_detail, 1);
    ASSERT_TRUE(ix.ok);
    ASSERT_EQ(det.kind, SPECIES_WILD);
    ASSERT_EQ(det.rows_species, 1);
    ASSERT_EQ(det.step_count, 0);
    ASSERT_EQ(det.row_count, 1);
    ASSERT_EQ(det.row_total, 1);
    check_row(&det.rows[0], ENC_LAND, 10, "ROUTE 101", 3, 8, 10, 40, 0);
}

TEST(detail_rows_ordered_by_method) {
    build_detail();
    detail_of(&k_detail, S_MARILL);
    ASSERT_EQ(det.kind, SPECIES_WILD);
    ASSERT_EQ(det.row_count, 9);
    ASSERT_EQ(det.row_total, 9);
    check_row(&det.rows[0], ENC_WATER, 11, "ROUTE 102", 10, 20, 100, 100, 0);
    check_row(&det.rows[1], ENC_WATER, 12, "ROUTE 119", 20, 30, 99, 99, 0);
    check_row(&det.rows[2], ENC_WATER, 13, "ROUTE 130", 30, 35, 100, 100, 0);
    check_row(&det.rows[3], ENC_OLD_ROD, 11, "ROUTE 102", 5, 10, 100, 100, 0);
    check_row(&det.rows[4], ENC_OLD_ROD, 12, "ROUTE 119", 25, 30, 100, 100, 0);
    check_row(&det.rows[5], ENC_GOOD_ROD, 11, "ROUTE 102", 5, 10, 100, 100, 0);
    check_row(&det.rows[6], ENC_GOOD_ROD, 12, "ROUTE 119", 25, 30, 100, 100, 0);
    check_row(&det.rows[7], ENC_SUPER_ROD, 11, "ROUTE 102", 5, 10, 100, 100, 0);
    check_row(&det.rows[8], ENC_SUPER_ROD, 12, "ROUTE 119", 25, 30, 100, 100, 0);
}

TEST(detail_dive_uses_surface_name) {
    build_detail();
    detail_of(&k_detail, S_CLAMPERL);
    ASSERT_EQ(det.kind, SPECIES_WILD);
    ASSERT_EQ(det.row_count, 4);
    check_row(&det.rows[0], ENC_WATER, 12, "ROUTE 119", 20, 20, 1, 1, 0);
    check_row(&det.rows[1], SROW_DIVE, 39, "ROUTE 124", 20, 30, 100, 100, 0);
    check_row(&det.rows[2], SROW_DIVE, 41, "ROUTE 126", 25, 35, 100, 100, 0);
    check_row(&det.rows[3], ENC_ROCK_SMASH, 11, "ROUTE 102", 15, 15, 100, 100, 0);
}

TEST(detail_mirage_and_postgame_flags) {
    build_detail();
    detail_of(&k_detail, S_WYNAUT);
    ASSERT_EQ(det.kind, SPECIES_WILD);
    ASSERT_EQ(det.row_count, 1);
    check_row(&det.rows[0], ENC_LAND, 71, "MIRAGE ISLAND", 5, 10, 100, 100, SROW_MIRAGE);

    detail_of(&k_detail, S_DITTO);
    ASSERT_EQ(det.row_count, 3);
    check_row(&det.rows[0], ENC_LAND, 20, "SAFARI ZONE", 30, 38, 20, 100, SROW_POSTGAME);
    check_row(&det.rows[1], ENC_LAND, 20, "SAFARI ZONE", 10, 10, 1, 1, 0);
    check_row(&det.rows[2], ENC_LAND, 67, "SHOAL CAVE", 20, 22, 40, 40, SROW_LOW_TIDE);
}

TEST(detail_evolve_chain_to_wild) {
    build_detail();
    detail_of(&k_detail, 3);
    ASSERT_EQ(det.species, 3);
    ASSERT_EQ(det.kind, SPECIES_EVOLVE);
    ASSERT_EQ(det.rows_species, 1);
    ASSERT_EQ(det.step_count, 2);
    ASSERT_EQ(det.steps[0].from, 2);
    ASSERT_EQ(det.steps[0].method, 4);
    ASSERT_EQ(det.steps[0].param, 30);
    ASSERT_STR_EQ(det.steps[0].item, "");
    ASSERT_EQ(det.steps[1].from, 1);
    ASSERT_EQ(det.steps[1].param, 20);
    ASSERT_EQ(det.row_count, 1);
    check_row(&det.rows[0], ENC_LAND, 10, "ROUTE 101", 3, 8, 10, 40, 0);
}

TEST(detail_evolve_item_step) {
    build_detail();
    detail_of(&k_detail, 5);   /* MEWTWO: only in an unreachable header */
    ASSERT_EQ(det.kind, SPECIES_EVOLVE);
    ASSERT_EQ(det.rows_species, 4);
    ASSERT_EQ(det.step_count, 1);
    ASSERT_EQ(det.steps[0].method, 7);
    ASSERT_EQ(det.steps[0].param, 93);
    ASSERT_STR_EQ(det.steps[0].item, "SUN STONE");
    ASSERT_EQ(det.row_count, 1);
    check_row(&det.rows[0], ENC_LAND, 11, "ROUTE 102", 5, 5, 100, 100, 0);
}

TEST(detail_milotic_gets_feebas_row) {
    build_detail();
    detail_of(&k_detail, S_MILOTIC);
    ASSERT_EQ(det.kind, SPECIES_EVOLVE);
    ASSERT_EQ(det.rows_species, 11);
    ASSERT_EQ(det.step_count, 1);
    ASSERT_EQ(det.steps[0].from, 11);
    ASSERT_EQ(det.steps[0].method, 15);
    ASSERT_EQ(det.steps[0].param, 170);
    ASSERT_EQ(det.row_count, 1);
    check_row(&det.rows[0], SROW_ANY_ROD, 12, "ROUTE 119", 20, 25, 50, 50, SROW_FEEBAS_TILES);

    detail_of(&k_detail, 11);
    ASSERT_EQ(det.kind, SPECIES_WILD);
    ASSERT_EQ(det.row_count, 1);
    ASSERT_EQ(det.rows[0].flags, SROW_FEEBAS_TILES);
}

TEST(detail_breed_with_incense) {
    build_detail();
    detail_of(&k_detail, S_AZURILL);
    ASSERT_EQ(det.kind, SPECIES_BREED);
    ASSERT_EQ(det.rows_species, S_MARILL);
    ASSERT_EQ(det.step_count, 0);
    ASSERT_STR_EQ(det.incense, "SEA INCENSE");
    ASSERT_EQ(det.row_count, 9);
    ASSERT_EQ(det.rows[0].mapsec, 11);
}

TEST(detail_not_wild) {
    build_detail();
    detail_of(&k_detail, 8);              /* MR. MIME: no family at all */
    ASSERT_EQ(det.kind, SPECIES_NOT_WILD);
    ASSERT_EQ(det.row_count, 0);
    ASSERT_EQ(det.row_total, 0);
    detail_of(&k_detail, S_CHIKORITA);    /* evolves into a non-wild species */
    ASSERT_EQ(det.kind, SPECIES_NOT_WILD);
    ASSERT_STR_EQ(det.incense, "");
    detail_of(&k_detail, 10);             /* BAYLEEF: root CHIKORITA is not wild */
    ASSERT_EQ(det.kind, SPECIES_EVOLVE_NOT_WILD);
    ASSERT_EQ(det.step_count, 1);
    ASSERT_EQ(det.steps[0].from, S_CHIKORITA);
    ASSERT_EQ(det.steps[0].param, 16);
    ASSERT_EQ(det.row_count, 0);
}

TEST(detail_parent_cycle_terminates) {
    build_detail();
    detail_of(&k_detail, S_SNORUNT);
    ASSERT_EQ(det.kind, SPECIES_EVOLVE_NOT_WILD);
    ASSERT_EQ(det.step_count, 3);
    ASSERT_EQ(det.steps[0].from, S_GLALIE);
    ASSERT_EQ(det.steps[0].param, 43);
    ASSERT_EQ(det.steps[1].from, S_SNORUNT);
    ASSERT_TRUE(det.steps_truncated);
    ASSERT_EQ(det.row_count, 0);
}

TEST(detail_bad_species_is_empty) {
    build_detail();
    detail_of(&k_detail, 500);
    ASSERT_EQ(det.species, 500);
    ASSERT_EQ(det.kind, SPECIES_NOT_WILD);
    ASSERT_EQ(det.row_count, 0);
}

TEST(detail_truncates_and_counts) {
    build_detail();
    GameProfile p = k_detail;
    p.wild_mon_headers = THDR;
    p.wild_header_cap = 140;
    p.map_groups = TMAPGROUPS;
    p.map_group_count = 1;
    p.mirage_map_num = 200;   /* keep every map a plain route */
    s_table_next = TINFO;
    uint32_t info = table(12, S_DITTO, 5, 5);
    for (uint32_t i = 0; i < 130; i++) {
        fixture_put32(at(TGROUP + 4u * i), TMAPHDRS + 0x1Cu * i);
        at(TMAPHDRS + 0x1Cu * i + 0x14)[0] = (uint8_t)i;
        header2(THDR, (int)i, 0, (uint8_t)i, info, 0, 0, 0);
    }
    fixture_put32(at(TMAPGROUPS), TGROUP);
    at(THDR + 20u * 130)[0] = 0xFF;
    detail_of(&p, S_DITTO);
    ASSERT_EQ(det.kind, SPECIES_WILD);
    ASSERT_EQ(det.row_count, SPECIES_ROWS_MAX);
    ASSERT_EQ(det.row_total, 130);
    ASSERT_EQ(det.rows[0].mapsec, 0);
    ASSERT_EQ(det.rows[127].mapsec, 127);
    ASSERT_EQ(det.rows[127].pct_hi, 100);
}

TEST(detail_rows_sorted_by_mapsec_within_method) {
    build_detail();
    GameProfile p = k_detail;
    p.wild_mon_headers = THDR;
    p.wild_header_cap = 140;
    p.map_groups = TMAPGROUPS;
    p.map_group_count = 1;
    p.mirage_map_num = 200;
    s_table_next = TINFO;
    uint32_t info = table(12, S_DITTO, 5, 5);
    static const uint8_t secs[4] = {30, 10, 20, 10};   /* appearance order is not mapsec order */
    for (uint32_t i = 0; i < 4; i++) {
        fixture_put32(at(TGROUP + 4u * i), TMAPHDRS + 0x1Cu * i);
        at(TMAPHDRS + 0x1Cu * i + 0x14)[0] = secs[i];
        header2(THDR, (int)i, 0, (uint8_t)i, info, 0, 0, 0);
    }
    fixture_put32(at(TMAPGROUPS), TGROUP);
    at(THDR + 20u * 4)[0] = 0xFF;
    detail_of(&p, S_DITTO);
    ASSERT_EQ(det.row_count, 3);
    ASSERT_EQ(det.row_total, 3);
    ASSERT_EQ(det.rows[0].mapsec, 10);
    ASSERT_EQ(det.rows[1].mapsec, 20);
    ASSERT_EQ(det.rows[2].mapsec, 30);
}

/* FRLG has no Feebas tiles: the index must still build (Search and the egg guess need it). */
TEST(index_without_feebas_feature_skips_the_feebas_read) {
    build();
    GameMem m = mem();
    species_index_build(&ix, &k_profile, &m);
    ASSERT_TRUE(ix.ok);
    ASSERT_TRUE(species_index_is_wild(&ix, 11));    /* Feebas, from sWildFeebas */
    static GameRules r;
    r = g_emerald_rules;
    r.features = 0;
    GameProfile p = k_profile;
    p.rules = &r;
    p.wild_feebas = 0;
    species_index_build(&ix, &p, &m);
    ASSERT_TRUE(ix.ok);
    ASSERT_TRUE(!species_index_is_wild(&ix, 11));
}

/* FRLG fills Emerald-only map fields with 0xFF; map_sec's own "unknown" is 0xFF too. */
TEST(detail_sentinels_never_match_unknown_sections) {
    build_detail();
    static GameRules r;
    r = g_emerald_rules;
    r.features = 0;
    GameProfile p = k_detail;
    p.rules = &r;
    p.map_group_count = 1;                       /* group 1's maps have no section: 0xFF */
    p.wild_feebas = 0;
    p.feebas_map_group = p.feebas_map_num = 0xFF;
    p.mirage_map_group = p.mirage_map_num = p.mirage_mapsec = 0xFF;
    memset(p.dive_map, 0xFF, sizeof(p.dive_map));
    memset(p.dive_surface_mapsec, 0xFF, sizeof(p.dive_surface_mapsec));
    memset(p.postgame_map, 0xFF, sizeof(p.postgame_map));
    p.low_tide_mapsec = 0xFF;
    detail_of(&p, S_DITTO);
    ASSERT_EQ(det.kind, SPECIES_WILD);
    ASSERT_TRUE(det.row_count > 0);
    for (uint16_t i = 0; i < det.row_count; i++) ASSERT_EQ(det.rows[i].flags, 0);
}

TEST(detail_marks_evolutions_this_game_cannot_do) {
    build_detail();
    detail_of(&k_detail, S_MILOTIC);
    ASSERT_TRUE(det.step_count > 0);
    ASSERT_EQ(det.steps[0].method, 15);           /* EVO_BEAUTY */
    ASSERT_TRUE(!det.steps[0].impossible);
    static GameRules r;
    r = g_emerald_rules;
    r.evo_day_night_beauty = false;
    GameProfile p = k_detail;
    p.rules = &r;
    detail_of(&p, S_MILOTIC);
    ASSERT_TRUE(det.steps[0].impossible);
}

void run_species_search_tests(void) {
    RUN_TEST(index_builds_names_and_gender);
    RUN_TEST(index_skips_placeholders);
    RUN_TEST(index_parent_from_8_byte_stride);
    RUN_TEST(index_wild_first_header_per_map);
    RUN_TEST(query_exact_then_prefix_then_substring);
    RUN_TEST(query_punctuation_only_is_empty);
    RUN_TEST(index_rejects_bad_tables);
    RUN_TEST(index_without_feebas_feature_skips_the_feebas_read);
    RUN_TEST(detail_wild_rows_merged);
    RUN_TEST(detail_rows_ordered_by_method);
    RUN_TEST(detail_dive_uses_surface_name);
    RUN_TEST(detail_mirage_and_postgame_flags);
    RUN_TEST(detail_evolve_chain_to_wild);
    RUN_TEST(detail_evolve_item_step);
    RUN_TEST(detail_milotic_gets_feebas_row);
    RUN_TEST(detail_breed_with_incense);
    RUN_TEST(detail_not_wild);
    RUN_TEST(detail_parent_cycle_terminates);
    RUN_TEST(detail_rows_sorted_by_mapsec_within_method);
    RUN_TEST(detail_bad_species_is_empty);
    RUN_TEST(detail_truncates_and_counts);
    RUN_TEST(detail_sentinels_never_match_unknown_sections);
    RUN_TEST(detail_marks_evolutions_this_game_cannot_do);
}
