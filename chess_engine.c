#include <stdint.h>
#include <stdio.h>

enum { WP, WN, WB, WR, WQ, WK, BP, BN, BB, BR, BQ, BK };
enum { WHITE, BLACK };
enum { PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING };

// Move flags
#define FLAG_QUIET      0
#define FLAG_DOUBLE     1
#define FLAG_KS_CASTLE  2
#define FLAG_QS_CASTLE  3
#define FLAG_CAPTURE    4
#define FLAG_EP         5
#define FLAG_PROMO_N    8
#define FLAG_PROMO_B    9
#define FLAG_PROMO_R    10
#define FLAG_PROMO_Q    11
#define FLAG_PROMO_CAP_N 12
#define FLAG_PROMO_CAP_B 13
#define FLAG_PROMO_CAP_R 14
#define FLAG_PROMO_CAP_Q 15

#define CASTLE_WK 1
#define CASTLE_WQ 2
#define CASTLE_BK 4
#define CASTLE_BQ 8

#define NO_CAPTURE 7

typedef struct {
    uint64_t pieces[12];
    uint32_t flags;  // bit0: side, bits1-4: castling, bits5-8: ep_file (15=none)
} Board;

// Move (32-bit):
// bits 0-5:   from square 2^6 = 64
// bits 6-11:  to square
// bits 12-15: flags
// bits 16-18: piece moved (0-5) 2^3 = 8
// bits 19-21: captured piece (0-5, 7=none)
// bits 22-25: prev castling 2^4 = 16
// bits 26-29: prev ep file
typedef uint32_t Move;

#define MOVE_FROM(m)       ((m) & 0x3F)
#define MOVE_TO(m)         (((m) >> 6) & 0x3F)
#define MOVE_FLAGS(m)      (((m) >> 12) & 0xF)
#define MOVE_PIECE(m)      (((m) >> 16) & 0x7)
#define MOVE_CAPTURED(m)   (((m) >> 19) & 0x7)
#define MOVE_PREV_CAST(m)  (((m) >> 22) & 0xF)
#define MOVE_PREV_EP(m)    (((m) >> 26) & 0xF)

// Create move (piece, captured, prev_cast, prev_ep filled later by make_move)
#define MOVE_NEW(from, to, flag, piece) \
    ((Move)((from) | ((to) << 6) | ((flag) << 12) | ((piece) << 16) | (NO_CAPTURE << 19)))

#define SIDE(b)     ((b)->flags & 1)
#define CASTLING(b) (((b)->flags >> 1) & 0xF)
#define EP_FILE(b)  (((b)->flags >> 5) & 0xF)

#define SET_SIDE(b, s)     ((b)->flags = ((b)->flags & ~1u) | ((s) & 1))
#define SET_CASTLING(b, c) ((b)->flags = ((b)->flags & ~0x1Eu) | (((c) & 0xF) << 1))
#define SET_EP_FILE(b, e)  ((b)->flags = ((b)->flags & ~0x1E0u) | (((e) & 0xF) << 5))

// Precomputed attack tables
static uint64_t KNIGHT_ATTACKS[64];
static uint64_t KING_ATTACKS[64];
static uint64_t PAWN_ATTACKS[2][64];  // [side][square]
static uint64_t RAY_ATTACKS[8][64];   // [direction][square]

// direction for rays
enum { NORTH, SOUTH, EAST, WEST, NE, NW, SE, SW };

#define BIT(sq)       (1ULL << (sq))
#define RANK(sq)      ((sq) >> 3)
#define FILE(sq)      ((sq) & 7)
#define SQ(file,rank) ((rank) * 8 + (file))

static inline int popcount(uint64_t b) { return __builtin_popcountll(b); }
static inline int lsb(uint64_t b) { return __builtin_ctzll(b); }
static inline int msb(uint64_t b) { return 63 - __builtin_clzll(b); }
static inline int pop_lsb(uint64_t *b) {
    int sq = lsb(*b);
    *b &= *b - 1;
    return sq;
}

static void init_knight_attacks(void) {
    for (int sq = 0; sq < 64; sq++) {
        uint64_t b = 0;
        int r = RANK(sq), f = FILE(sq);
        if (r < 6 && f < 7) b |= BIT(sq + 17);
        if (r < 6 && f > 0) b |= BIT(sq + 15);
        if (r < 7 && f < 6) b |= BIT(sq + 10);
        if (r < 7 && f > 1) b |= BIT(sq + 6);
        if (r > 0 && f < 6) b |= BIT(sq - 6);
        if (r > 0 && f > 1) b |= BIT(sq - 10);
        if (r > 1 && f < 7) b |= BIT(sq - 15);
        if (r > 1 && f > 0) b |= BIT(sq - 17);
        KNIGHT_ATTACKS[sq] = b;
    }
}

static void init_king_attacks(void) {
    for (int sq = 0; sq < 64; sq++) {
        uint64_t b = 0;
        int r = RANK(sq), f = FILE(sq);
        if (r < 7) b |= BIT(sq + 8);
        if (r > 0) b |= BIT(sq - 8);
        if (f < 7) b |= BIT(sq + 1);
        if (f > 0) b |= BIT(sq - 1);
        if (r < 7 && f < 7) b |= BIT(sq + 9);
        if (r < 7 && f > 0) b |= BIT(sq + 7);
        if (r > 0 && f < 7) b |= BIT(sq - 7);
        if (r > 0 && f > 0) b |= BIT(sq - 9);
        KING_ATTACKS[sq] = b;
    }
}

static void init_pawn_attacks(void) {
    for (int sq = 0; sq < 64; sq++) {
        int r = RANK(sq), f = FILE(sq);
        PAWN_ATTACKS[WHITE][sq] = 0;
        PAWN_ATTACKS[BLACK][sq] = 0;
        if (r < 7) {
            if (f > 0) PAWN_ATTACKS[WHITE][sq] |= BIT(sq + 7);
            if (f < 7) PAWN_ATTACKS[WHITE][sq] |= BIT(sq + 9);
        }
        if (r > 0) {
            if (f > 0) PAWN_ATTACKS[BLACK][sq] |= BIT(sq - 9);
            if (f < 7) PAWN_ATTACKS[BLACK][sq] |= BIT(sq - 7);
        }
    }
}

static void init_ray_attacks(void) {
    for (int sq = 0; sq < 64; sq++) {
        int r = RANK(sq), f = FILE(sq);

        RAY_ATTACKS[NORTH][sq] = 0;
        for (int i = r + 1; i < 8; i++) RAY_ATTACKS[NORTH][sq] |= BIT(SQ(f, i));

        RAY_ATTACKS[SOUTH][sq] = 0;
        for (int i = r - 1; i >= 0; i--) RAY_ATTACKS[SOUTH][sq] |= BIT(SQ(f, i));

        RAY_ATTACKS[EAST][sq] = 0;
        for (int i = f + 1; i < 8; i++) RAY_ATTACKS[EAST][sq] |= BIT(SQ(i, r));

        RAY_ATTACKS[WEST][sq] = 0;
        for (int i = f - 1; i >= 0; i--) RAY_ATTACKS[WEST][sq] |= BIT(SQ(i, r));

        RAY_ATTACKS[NE][sq] = 0;
        for (int i = 1; f + i < 8 && r + i < 8; i++) RAY_ATTACKS[NE][sq] |= BIT(SQ(f+i, r+i));

        RAY_ATTACKS[NW][sq] = 0;
        for (int i = 1; f - i >= 0 && r + i < 8; i++) RAY_ATTACKS[NW][sq] |= BIT(SQ(f-i, r+i));

        RAY_ATTACKS[SE][sq] = 0;
        for (int i = 1; f + i < 8 && r - i >= 0; i++) RAY_ATTACKS[SE][sq] |= BIT(SQ(f+i, r-i));

        RAY_ATTACKS[SW][sq] = 0;
        for (int i = 1; f - i >= 0 && r - i >= 0; i++) RAY_ATTACKS[SW][sq] |= BIT(SQ(f-i, r-i));
    }
}

static int initialized = 0;
static void init_tables(void) {
    if (initialized) return;
    init_knight_attacks();
    init_king_attacks();
    init_pawn_attacks();
    init_ray_attacks();
    initialized = 1;
}

// Sliding piece attacks
static inline uint64_t ray_attack(int sq, int dir, uint64_t occ) {
    uint64_t ray = RAY_ATTACKS[dir][sq];
    uint64_t blockers = ray & occ;
    if (blockers) {
        int blocker_sq;
        if (dir == NORTH || dir == EAST || dir == NE || dir == NW) {
            blocker_sq = lsb(blockers);
        } else {
            blocker_sq = msb(blockers);
        }
        ray &= ~RAY_ATTACKS[dir][blocker_sq];
    }
    return ray;
}

static inline uint64_t bishop_attacks(int sq, uint64_t occ) {
    return ray_attack(sq, NE, occ) | ray_attack(sq, NW, occ) |
           ray_attack(sq, SE, occ) | ray_attack(sq, SW, occ);
}

static inline uint64_t rook_attacks(int sq, uint64_t occ) {
    return ray_attack(sq, NORTH, occ) | ray_attack(sq, SOUTH, occ) |
           ray_attack(sq, EAST, occ) | ray_attack(sq, WEST, occ);
}

static inline uint64_t queen_attacks(int sq, uint64_t occ) {
    return bishop_attacks(sq, occ) | rook_attacks(sq, occ);
}

static inline uint64_t white_occ(const Board *b) {
    return b->pieces[WP] | b->pieces[WN] | b->pieces[WB] |
           b->pieces[WR] | b->pieces[WQ] | b->pieces[WK];
}

static inline uint64_t black_occ(const Board *b) {
    return b->pieces[BP] | b->pieces[BN] | b->pieces[BB] |
           b->pieces[BR] | b->pieces[BQ] | b->pieces[BK];
}

static inline uint64_t all_occ(const Board *b) {
    return white_occ(b) | black_occ(b);
}

static int is_attacked(const Board *b, int sq, int by_side) {
    uint64_t occ = all_occ(b);

    if (by_side == WHITE) {
        if (PAWN_ATTACKS[BLACK][sq] & b->pieces[WP]) return 1;
        if (KNIGHT_ATTACKS[sq] & b->pieces[WN]) return 1;
        if (bishop_attacks(sq, occ) & (b->pieces[WB] | b->pieces[WQ])) return 1;
        if (rook_attacks(sq, occ) & (b->pieces[WR] | b->pieces[WQ])) return 1;
        if (KING_ATTACKS[sq] & b->pieces[WK]) return 1;
    } else {
        if (PAWN_ATTACKS[WHITE][sq] & b->pieces[BP]) return 1;
        if (KNIGHT_ATTACKS[sq] & b->pieces[BN]) return 1;
        if (bishop_attacks(sq, occ) & (b->pieces[BB] | b->pieces[BQ])) return 1;
        if (rook_attacks(sq, occ) & (b->pieces[BR] | b->pieces[BQ])) return 1;
        if (KING_ATTACKS[sq] & b->pieces[BK]) return 1;
    }
    return 0;
}

static int add_moves(Move *list, int count, int from, uint64_t targets, int flag, int piece) {
    while (targets) {
        int to = pop_lsb(&targets);
        list[count++] = MOVE_NEW(from, to, flag, piece);
    }
    return count;
}

static int add_pawn_moves(Move *list, int count, int from, int to, int is_capture) {
    int rank_to = RANK(to);
    if (rank_to == 0 || rank_to == 7) {
        int base_flag = is_capture ? FLAG_PROMO_CAP_N : FLAG_PROMO_N;
        list[count++] = MOVE_NEW(from, to, base_flag, PAWN);
        list[count++] = MOVE_NEW(from, to, base_flag + 1, PAWN);
        list[count++] = MOVE_NEW(from, to, base_flag + 2, PAWN);
        list[count++] = MOVE_NEW(from, to, base_flag + 3, PAWN);
    } else {
        list[count++] = MOVE_NEW(from, to, is_capture ? FLAG_CAPTURE : FLAG_QUIET, PAWN);
    }
    return count;
}

int generate_moves(const Board *b, Move *list) {
    init_tables();

    int count = 0;
    int side = SIDE(b);
    uint64_t us = side == WHITE ? white_occ(b) : black_occ(b);
    uint64_t them = side == WHITE ? black_occ(b) : white_occ(b);
    uint64_t occ = us | them;
    uint64_t empty = ~occ;

    int piece_offset = side * 6;

    // Pawns
    uint64_t pawns = b->pieces[piece_offset + PAWN];
    if (side == WHITE) {
        uint64_t push1 = (pawns << 8) & empty;
        uint64_t tmp = push1;
        while (tmp) {
            int to = pop_lsb(&tmp);
            count = add_pawn_moves(list, count, to - 8, to, 0);
        }
        uint64_t push2 = ((push1 & 0x0000000000FF0000ULL) << 8) & empty;
        while (push2) {
            int to = pop_lsb(&push2);
            list[count++] = MOVE_NEW(to - 16, to, FLAG_DOUBLE, PAWN);
        }
        uint64_t cap_left = ((pawns & ~0x0101010101010101ULL) << 7) & them;
        while (cap_left) {
            int to = pop_lsb(&cap_left);
            count = add_pawn_moves(list, count, to - 7, to, 1);
        }
        uint64_t cap_right = ((pawns & ~0x8080808080808080ULL) << 9) & them;
        while (cap_right) {
            int to = pop_lsb(&cap_right);
            count = add_pawn_moves(list, count, to - 9, to, 1);
        }
        int ep_file = EP_FILE(b);
        if (ep_file < 8) {
            int ep_sq = SQ(ep_file, 5);
            uint64_t ep_attackers = PAWN_ATTACKS[BLACK][ep_sq] & pawns;
            while (ep_attackers) {
                int from = pop_lsb(&ep_attackers);
                list[count++] = MOVE_NEW(from, ep_sq, FLAG_EP, PAWN);
            }
        }
    } else {
        uint64_t push1 = (pawns >> 8) & empty;
        uint64_t tmp = push1;
        while (tmp) {
            int to = pop_lsb(&tmp);
            count = add_pawn_moves(list, count, to + 8, to, 0);
        }
        uint64_t push2 = ((push1 & 0x0000FF0000000000ULL) >> 8) & empty;
        while (push2) {
            int to = pop_lsb(&push2);
            list[count++] = MOVE_NEW(to + 16, to, FLAG_DOUBLE, PAWN);
        }
        uint64_t cap_left = ((pawns & ~0x8080808080808080ULL) >> 7) & them;
        while (cap_left) {
            int to = pop_lsb(&cap_left);
            count = add_pawn_moves(list, count, to + 7, to, 1);
        }
        uint64_t cap_right = ((pawns & ~0x0101010101010101ULL) >> 9) & them;
        while (cap_right) {
            int to = pop_lsb(&cap_right);
            count = add_pawn_moves(list, count, to + 9, to, 1);
        }
        int ep_file = EP_FILE(b);
        if (ep_file < 8) {
            int ep_sq = SQ(ep_file, 2);
            uint64_t ep_attackers = PAWN_ATTACKS[WHITE][ep_sq] & pawns;
            while (ep_attackers) {
                int from = pop_lsb(&ep_attackers);
                list[count++] = MOVE_NEW(from, ep_sq, FLAG_EP, PAWN);
            }
        }
    }

    // Knights
    uint64_t knights = b->pieces[piece_offset + KNIGHT];
    while (knights) {
        int from = pop_lsb(&knights);
        uint64_t attacks = KNIGHT_ATTACKS[from];
        count = add_moves(list, count, from, attacks & empty, FLAG_QUIET, KNIGHT);
        count = add_moves(list, count, from, attacks & them, FLAG_CAPTURE, KNIGHT);
    }

    // Bishops
    uint64_t bishops = b->pieces[piece_offset + BISHOP];
    while (bishops) {
        int from = pop_lsb(&bishops);
        uint64_t attacks = bishop_attacks(from, occ);
        count = add_moves(list, count, from, attacks & empty, FLAG_QUIET, BISHOP);
        count = add_moves(list, count, from, attacks & them, FLAG_CAPTURE, BISHOP);
    }

    // Rooks
    uint64_t rooks = b->pieces[piece_offset + ROOK];
    while (rooks) {
        int from = pop_lsb(&rooks);
        uint64_t attacks = rook_attacks(from, occ);
        count = add_moves(list, count, from, attacks & empty, FLAG_QUIET, ROOK);
        count = add_moves(list, count, from, attacks & them, FLAG_CAPTURE, ROOK);
    }

    // Queens
    uint64_t queens = b->pieces[piece_offset + QUEEN];
    while (queens) {
        int from = pop_lsb(&queens);
        uint64_t attacks = queen_attacks(from, occ);
        count = add_moves(list, count, from, attacks & empty, FLAG_QUIET, QUEEN);
        count = add_moves(list, count, from, attacks & them, FLAG_CAPTURE, QUEEN);
    }

    // King
    int king_sq = lsb(b->pieces[piece_offset + KING]);
    uint64_t king_attacks = KING_ATTACKS[king_sq];
    count = add_moves(list, count, king_sq, king_attacks & empty, FLAG_QUIET, KING);
    count = add_moves(list, count, king_sq, king_attacks & them, FLAG_CAPTURE, KING);

    // Castling
    int castling = CASTLING(b);
    int enemy = side ^ 1;
    if (side == WHITE) {
        if ((castling & CASTLE_WK) && !(occ & 0x60ULL) &&
            !is_attacked(b, 4, enemy) && !is_attacked(b, 5, enemy) && !is_attacked(b, 6, enemy)) {
            list[count++] = MOVE_NEW(4, 6, FLAG_KS_CASTLE, KING);
        }
        if ((castling & CASTLE_WQ) && !(occ & 0x0EULL) &&
            !is_attacked(b, 4, enemy) && !is_attacked(b, 3, enemy) && !is_attacked(b, 2, enemy)) {
            list[count++] = MOVE_NEW(4, 2, FLAG_QS_CASTLE, KING);
        }
    } else {
        if ((castling & CASTLE_BK) && !(occ & 0x6000000000000000ULL) &&
            !is_attacked(b, 60, enemy) && !is_attacked(b, 61, enemy) && !is_attacked(b, 62, enemy)) {
            list[count++] = MOVE_NEW(60, 62, FLAG_KS_CASTLE, KING);
        }
        if ((castling & CASTLE_BQ) && !(occ & 0x0E00000000000000ULL) &&
            !is_attacked(b, 60, enemy) && !is_attacked(b, 59, enemy) && !is_attacked(b, 58, enemy)) {
            list[count++] = MOVE_NEW(60, 58, FLAG_QS_CASTLE, KING);
        }
    }

    return count;
}

// Find which piece type (0-5) is on square for given side, returns -1 if none
static inline int piece_on_sq(const Board *b, int sq, int side) {
    uint64_t bit = BIT(sq);
    int offset = side * 6;
    for (int p = 0; p < 6; p++) {
        if (b->pieces[offset + p] & bit) return p;
    }
    return -1;
}

// Make move - modifies move to store captured piece and previous state
void make_move(Board *b, Move *m) {
    int from = MOVE_FROM(*m);
    int to = MOVE_TO(*m);
    int flags = MOVE_FLAGS(*m);
    int piece = MOVE_PIECE(*m);
    int side = SIDE(b);
    int enemy = side ^ 1;

    // Store previous state in move
    int prev_castling = CASTLING(b);
    int prev_ep = EP_FILE(b);
    *m = (*m & 0x003FFFFF) | (prev_castling << 22) | (prev_ep << 26);

    int captured = NO_CAPTURE;
    int cap_sq = to;

    if (flags == FLAG_EP) {
        cap_sq = side == WHITE ? to - 8 : to + 8;
        captured = PAWN;
        b->pieces[enemy * 6 + PAWN] &= ~BIT(cap_sq);
    } else if (flags == FLAG_CAPTURE || flags >= FLAG_PROMO_CAP_N) {
        captured = piece_on_sq(b, to, enemy);
        if (captured >= 0) {
            b->pieces[enemy * 6 + captured] &= ~BIT(to);
        } else {
            captured = NO_CAPTURE;
        }
    }
    *m = (*m & 0xFFC7FFFF) | (captured << 19);

    // Move piece
    int piece_idx = side * 6 + piece;
    b->pieces[piece_idx] &= ~BIT(from);
    b->pieces[piece_idx] |= BIT(to);

    if (flags >= FLAG_PROMO_N && flags <= FLAG_PROMO_Q) {
        b->pieces[piece_idx] &= ~BIT(to);
        int promo_piece = KNIGHT + (flags - FLAG_PROMO_N);
        b->pieces[side * 6 + promo_piece] |= BIT(to);
    } else if (flags >= FLAG_PROMO_CAP_N && flags <= FLAG_PROMO_CAP_Q) {
        b->pieces[piece_idx] &= ~BIT(to);
        int promo_piece = KNIGHT + (flags - FLAG_PROMO_CAP_N);
        b->pieces[side * 6 + promo_piece] |= BIT(to);
    }

    // Handle castling move
    if (flags == FLAG_KS_CASTLE) {
        int rook_idx = side * 6 + ROOK;
        if (side == WHITE) {
            b->pieces[rook_idx] &= ~BIT(7);
            b->pieces[rook_idx] |= BIT(5);
        } else {
            b->pieces[rook_idx] &= ~BIT(63);
            b->pieces[rook_idx] |= BIT(61);
        }
    } else if (flags == FLAG_QS_CASTLE) {
        int rook_idx = side * 6 + ROOK;
        if (side == WHITE) {
            b->pieces[rook_idx] &= ~BIT(0);
            b->pieces[rook_idx] |= BIT(3);
        } else {
            b->pieces[rook_idx] &= ~BIT(56);
            b->pieces[rook_idx] |= BIT(59);
        }
    }

    int castling = prev_castling;
    if (piece == KING) {
        if (side == WHITE) castling &= ~(CASTLE_WK | CASTLE_WQ);
        else castling &= ~(CASTLE_BK | CASTLE_BQ);
    }
    if (piece == ROOK) {
        if (from == 0) castling &= ~CASTLE_WQ;
        if (from == 7) castling &= ~CASTLE_WK;
        if (from == 56) castling &= ~CASTLE_BQ;
        if (from == 63) castling &= ~CASTLE_BK;
    }
    if (to == 0) castling &= ~CASTLE_WQ;
    if (to == 7) castling &= ~CASTLE_WK;
    if (to == 56) castling &= ~CASTLE_BQ;
    if (to == 63) castling &= ~CASTLE_BK;
    SET_CASTLING(b, castling);

    if (flags == FLAG_DOUBLE) {
        SET_EP_FILE(b, FILE(to));
    } else {
        SET_EP_FILE(b, 15);
    }

    SET_SIDE(b, enemy);
}

void unmake_move(Board *b, Move m) {
    int from = MOVE_FROM(m);
    int to = MOVE_TO(m);
    int flags = MOVE_FLAGS(m);
    int piece = MOVE_PIECE(m);
    int captured = MOVE_CAPTURED(m);
    int prev_castling = MOVE_PREV_CAST(m);
    int prev_ep = MOVE_PREV_EP(m);

    int side = SIDE(b) ^ 1;
    int enemy = side ^ 1;

    SET_SIDE(b, side);
    SET_CASTLING(b, prev_castling);
    SET_EP_FILE(b, prev_ep);

    // Handle promotion - remove promoted piece, restore pawn
    if (flags >= FLAG_PROMO_N && flags <= FLAG_PROMO_Q) {
        int promo_piece = KNIGHT + (flags - FLAG_PROMO_N);
        b->pieces[side * 6 + promo_piece] &= ~BIT(to);
        b->pieces[side * 6 + PAWN] |= BIT(to);
    } else if (flags >= FLAG_PROMO_CAP_N && flags <= FLAG_PROMO_CAP_Q) {
        int promo_piece = KNIGHT + (flags - FLAG_PROMO_CAP_N);
        b->pieces[side * 6 + promo_piece] &= ~BIT(to);
        b->pieces[side * 6 + PAWN] |= BIT(to);
    }

    // Move piece back
    int piece_idx = side * 6 + piece;
    b->pieces[piece_idx] &= ~BIT(to);
    b->pieces[piece_idx] |= BIT(from);

    // Restore captured piece
    if (flags == FLAG_EP) {
        int cap_sq = side == WHITE ? to - 8 : to + 8;
        b->pieces[enemy * 6 + PAWN] |= BIT(cap_sq);
    } else if (captured != NO_CAPTURE) {
        b->pieces[enemy * 6 + captured] |= BIT(to);
    }

    // Undo castling rook move
    if (flags == FLAG_KS_CASTLE) {
        int rook_idx = side * 6 + ROOK;
        if (side == WHITE) {
            b->pieces[rook_idx] &= ~BIT(5);
            b->pieces[rook_idx] |= BIT(7);
        } else {
            b->pieces[rook_idx] &= ~BIT(61);
            b->pieces[rook_idx] |= BIT(63);
        }
    } else if (flags == FLAG_QS_CASTLE) {
        int rook_idx = side * 6 + ROOK;
        if (side == WHITE) {
            b->pieces[rook_idx] &= ~BIT(3);
            b->pieces[rook_idx] |= BIT(0);
        } else {
            b->pieces[rook_idx] &= ~BIT(59);
            b->pieces[rook_idx] |= BIT(56);
        }
    }
}

// Check if current side's king is in check
int in_check(const Board *b) {
    int side = SIDE(b);
    int king_sq = lsb(b->pieces[side * 6 + KING]);
    return is_attacked(b, king_sq, side ^ 1);
}

// Generate legal moves
int generate_legal_moves(const Board *b, Move *list) {
    Move pseudo[256];
    int pseudo_count = generate_moves(b, pseudo);

    int count = 0;
    int side = SIDE(b);
    int king_idx = side * 6 + KING;

    for (int i = 0; i < pseudo_count; i++) {
        Board copy = *b;
        Move m = pseudo[i];
        make_move(&copy, &m);

        int our_king_sq = lsb(copy.pieces[king_idx]);
        if (!is_attacked(&copy, our_king_sq, side ^ 1)) {
            list[count++] = pseudo[i];
        }
    }

    return count;
}

// Perft
uint64_t perft(Board *b, int depth) {
    if (depth == 0) return 1;

    Move moves[256];
    int count = generate_legal_moves(b, moves);

    if (depth == 1) return count;

    uint64_t nodes = 0;
    for (int i = 0; i < count; i++) {
        Move m = moves[i];
        make_move(b, &m);
        nodes += perft(b, depth - 1);
        unmake_move(b, m);
    }

    return nodes;
}

// ============================================================================
// EVALUATION
// ============================================================================

static const int PIECE_VALUES[6] = { 100, 320, 330, 500, 900, 20000 };

// Piece-square tables (from white's perspective, index 0 = a1)
static const int PST_PAWN[64] = {
     0,  0,  0,  0,  0,  0,  0,  0,
     5, 10, 10,-20,-20, 10, 10,  5,
     5, -5,-10,  0,  0,-10, -5,  5,
     0,  0,  0, 20, 20,  0,  0,  0,
     5,  5, 10, 25, 25, 10,  5,  5,
    10, 10, 20, 30, 30, 20, 10, 10,
    50, 50, 50, 50, 50, 50, 50, 50,
     0,  0,  0,  0,  0,  0,  0,  0
};

static const int PST_KNIGHT[64] = {
   -50,-40,-30,-30,-30,-30,-40,-50,
   -40,-20,  0,  5,  5,  0,-20,-40,
   -30,  5, 10, 15, 15, 10,  5,-30,
   -30,  0, 15, 20, 20, 15,  0,-30,
   -30,  5, 15, 20, 20, 15,  5,-30,
   -30,  0, 10, 15, 15, 10,  0,-30,
   -40,-20,  0,  0,  0,  0,-20,-40,
   -50,-40,-30,-30,-30,-30,-40,-50
};

static const int PST_BISHOP[64] = {
   -20,-10,-10,-10,-10,-10,-10,-20,
   -10,  5,  0,  0,  0,  0,  5,-10,
   -10, 10, 10, 10, 10, 10, 10,-10,
   -10,  0, 10, 10, 10, 10,  0,-10,
   -10,  5,  5, 10, 10,  5,  5,-10,
   -10,  0,  5, 10, 10,  5,  0,-10,
   -10,  0,  0,  0,  0,  0,  0,-10,
   -20,-10,-10,-10,-10,-10,-10,-20
};

static const int PST_ROOK[64] = {
     0,  0,  0,  5,  5,  0,  0,  0,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
     5, 10, 10, 10, 10, 10, 10,  5,
     0,  0,  0,  0,  0,  0,  0,  0
};

static const int PST_QUEEN[64] = {
   -20,-10,-10, -5, -5,-10,-10,-20,
   -10,  0,  5,  0,  0,  0,  0,-10,
   -10,  5,  5,  5,  5,  5,  0,-10,
     0,  0,  5,  5,  5,  5,  0, -5,
    -5,  0,  5,  5,  5,  5,  0, -5,
   -10,  0,  5,  5,  5,  5,  0,-10,
   -10,  0,  0,  0,  0,  0,  0,-10,
   -20,-10,-10, -5, -5,-10,-10,-20
};


static const int PST_KING_MG[64] = {
    20, 30, 10,  0,  0, 10, 30, 20,
    20, 20,  0,  0,  0,  0, 20, 20,
   -10,-20,-20,-20,-20,-20,-20,-10,
   -20,-30,-30,-40,-40,-30,-30,-20,
   -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30
};

static const int PST_KING_EG[64] = {
   -50,-30,-30,-30,-30,-30,-30,-50,
   -30,-30,  0,  0,  0,  0,-30,-30,
   -30,-10, 20, 30, 30, 20,-10,-30,
   -30,-10, 30, 40, 40, 30,-10,-30,
   -30,-10, 30, 40, 40, 30,-10,-30,
   -30,-10, 20, 30, 30, 20,-10,-30,
   -30,-20,-10,  0,  0,-10,-20,-30,
   -50,-40,-30,-20,-20,-30,-40,-50
};

static const int* PST[6] = { PST_PAWN, PST_KNIGHT, PST_BISHOP, PST_ROOK, PST_QUEEN, PST_KING_MG };

// Flip square for black (mirror vertically)
static inline int flip_sq(int sq) {
    return sq ^ 56;
}

static int count_phase(const Board *b) {
    int phase = 0;
    phase += popcount(b->pieces[WN]) + popcount(b->pieces[BN]);
    phase += popcount(b->pieces[WB]) + popcount(b->pieces[BB]);
    phase += 2 * (popcount(b->pieces[WR]) + popcount(b->pieces[BR]));
    phase += 4 * (popcount(b->pieces[WQ]) + popcount(b->pieces[BQ]));
    return phase;  // max = 24
}

static int evaluate(const Board *b) {
    int mg_score = 0;  // middlegame
    int eg_score = 0;  // endgame

    // White pieces
    for (int p = 0; p < 6; p++) {
        uint64_t pieces = b->pieces[p];
        while (pieces) {
            int sq = pop_lsb(&pieces);
            mg_score += PIECE_VALUES[p];
            eg_score += PIECE_VALUES[p];
            mg_score += PST[p][sq];
            if (p == KING) {
                eg_score += PST_KING_EG[sq];
            } else {
                eg_score += PST[p][sq];
            }
        }
    }

    // Black pieces
    for (int p = 0; p < 6; p++) {
        uint64_t pieces = b->pieces[6 + p];
        while (pieces) {
            int sq = pop_lsb(&pieces);
            mg_score -= PIECE_VALUES[p];
            eg_score -= PIECE_VALUES[p];
            mg_score -= PST[p][flip_sq(sq)];
            if (p == KING) {
                eg_score -= PST_KING_EG[flip_sq(sq)];
            } else {
                eg_score -= PST[p][flip_sq(sq)];
            }
        }
    }

    int phase = count_phase(b);
    int score = (mg_score * phase + eg_score * (24 - phase)) / 24;

    return SIDE(b) == WHITE ? score : -score;
}

// ============================================================================
// SEARCH
// ============================================================================

#include <time.h>

#define INF 100000
#define MATE_SCORE 90000

// Time management
static clock_t search_start;
static int time_limit_ms;
static int stop_search;

// MVV-LVA mov ordering
static inline int mvv_lva(Move m) {
    int flags = MOVE_FLAGS(m);
    if (flags == FLAG_CAPTURE || flags >= FLAG_PROMO_CAP_N) {
        int captured = MOVE_CAPTURED(m);
        int piece = MOVE_PIECE(m);
        if (captured != NO_CAPTURE) {
            return PIECE_VALUES[captured] * 10 - PIECE_VALUES[piece];
        }
    }
    if (flags >= FLAG_PROMO_N) {
        return 5000;
    }
    return 0;
}

// Sort moves by MVV-LVA
static void sort_moves(Move *moves, int count) {
    for (int i = 1; i < count; i++) {
        Move m = moves[i];
        int score = mvv_lva(m);
        int j = i - 1;
        while (j >= 0 && mvv_lva(moves[j]) < score) {
            moves[j + 1] = moves[j];
            j--;
        }
        moves[j + 1] = m;
    }
}

// Quiescence search - only search captures
static int quiescence(Board *b, int alpha, int beta) {
    int stand_pat = evaluate(b);

    if (stand_pat >= beta) return beta;
    if (stand_pat > alpha) alpha = stand_pat;

    Move moves[256];
    int count = generate_legal_moves(b, moves);

    // Filter to captures only
    int cap_count = 0;
    for (int i = 0; i < count; i++) {
        int flags = MOVE_FLAGS(moves[i]);
        if (flags == FLAG_CAPTURE || flags == FLAG_EP || flags >= FLAG_PROMO_CAP_N) {
            moves[cap_count++] = moves[i];
        }
    }

    sort_moves(moves, cap_count);

    for (int i = 0; i < cap_count; i++) {
        Move m = moves[i];
        make_move(b, &m);
        int score = -quiescence(b, -beta, -alpha);
        unmake_move(b, m);

        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
    }

    return alpha;
}

// Check if time is up
static inline int time_up(void) {
    if (time_limit_ms <= 0) return 0;
    clock_t elapsed = (clock() - search_start) * 1000 / CLOCKS_PER_SEC;
    return elapsed >= time_limit_ms;
}

static int negamax(Board *b, int depth, int alpha, int beta, int ply) {
    if (stop_search) return 0;

    if (depth <= 0) {
        return quiescence(b, alpha, beta);
    }

    // Check time_up near root of search
    if (ply < 3 && time_up()) {
        stop_search = 1;
        return 0;
    }

    Move moves[256];
    int count = generate_legal_moves(b, moves);

    if (count == 0) {
        if (in_check(b)) {
            return -MATE_SCORE + ply;  // checkmate, add ply to favor faster mates
        }
        return 0;  // stalemate
    }

    sort_moves(moves, count);

    for (int i = 0; i < count; i++) {
        Move m = moves[i];
        make_move(b, &m);
        int score = -negamax(b, depth - 1, -beta, -alpha, ply + 1);
        unmake_move(b, m);

        if (stop_search) return 0;

        if (score >= beta) {
            return beta;
        }
        if (score > alpha) {
            alpha = score;
        }
    }

    return alpha;
}

static Move search_best_move(Board *b, int depth) {
    Move moves[256];
    int count = generate_legal_moves(b, moves);

    if (count == 0) return 0;
    if (count == 1) return moves[0];

    sort_moves(moves, count);

    Move best_move = moves[0];
    int best_score = -INF;

    for (int i = 0; i < count; i++) {
        Move m = moves[i];
        make_move(b, &m);
        int score = -negamax(b, depth - 1, -INF, -best_score, 1);
        unmake_move(b, m);

        if (score > best_score) {
            best_score = score;
            best_move = moves[i];
        }
    }

    return best_move;
}

static void move_to_uci(Move m, char *buf) {
    int from = MOVE_FROM(m);
    int to = MOVE_TO(m);
    int flags = MOVE_FLAGS(m);

    buf[0] = 'a' + FILE(from);
    buf[1] = '1' + RANK(from);
    buf[2] = 'a' + FILE(to);
    buf[3] = '1' + RANK(to);
    buf[4] = '\0';

    if (flags >= FLAG_PROMO_N && flags <= FLAG_PROMO_Q) {
        buf[4] = "nbrq"[flags - FLAG_PROMO_N];
        buf[5] = '\0';
    } else if (flags >= FLAG_PROMO_CAP_N && flags <= FLAG_PROMO_CAP_Q) {
        buf[4] = "nbrq"[flags - FLAG_PROMO_CAP_N];
        buf[5] = '\0';
    }
}

// Format eval score for display
static void format_eval(int score, char *buf) {
    if (score > MATE_SCORE - 100) {
        int mate_in = (MATE_SCORE - score + 1) / 2;
        sprintf(buf, "#%d", mate_in);
    } else if (score < -MATE_SCORE + 100) {
        int mate_in = (MATE_SCORE + score + 1) / 2;
        sprintf(buf, "#-%d", mate_in);
    } else {
        sprintf(buf, "%+.2f", score / 100.0);
    }
}

static Move iterative_deepening(Board *b, int max_depth, int time_ms) {
    search_start = clock();
    time_limit_ms = time_ms;
    stop_search = 0;

    Move best_move = 0;
    Move moves[256];
    int count = generate_legal_moves(b, moves);

    if (count == 0) return 0;
    if (count == 1) return moves[0];

    int final_depth = 0;
    int final_score = 0;

    for (int depth = 1; depth <= max_depth; depth++) {
        sort_moves(moves, count);

        // Put previous best move first
        if (best_move != 0) {
            for (int i = 0; i < count; i++) {
                if ((moves[i] & 0xFFFF) == (best_move & 0xFFFF)) {
                    Move tmp = moves[0];
                    moves[0] = moves[i];
                    moves[i] = tmp;
                    break;
                }
            }
        }

        int best_score = -INF;
        Move iter_best = moves[0];

        for (int i = 0; i < count; i++) {
            Move m = moves[i];
            make_move(b, &m);
            int score = -negamax(b, depth - 1, -INF, -best_score, 1);
            unmake_move(b, m);

            if (stop_search) break;

            if (score > best_score) {
                best_score = score;
                iter_best = moves[i];
            }
        }

        // Only update best move if we completed this depth
        if (!stop_search) {
            best_move = iter_best;
            final_depth = depth;
            final_score = best_score;

            char move_str[6];
            char eval_str[16];
            move_to_uci(best_move, move_str);
            format_eval(best_score, eval_str);
            printf("  depth %2d  best %-5s  eval %s\n", depth, move_str, eval_str);
            fflush(stdout);
        }

        if (stop_search || time_up()) break;
    }

    return best_move;
}

// ============================================================================
// EXPORTS
// ============================================================================

__attribute__((visibility("default")))
void engine_init(void) {
    init_tables();
}

__attribute__((visibility("default")))
uint64_t engine_perft(uint64_t *board_data, int depth) {
    Board b;
    for (int i = 0; i < 12; i++) {
        b.pieces[i] = board_data[i];
    }
    b.flags = (uint32_t)board_data[12];

    return perft(&b, depth);
}

__attribute__((visibility("default")))
int engine_get_legal_moves(uint64_t *board_data, uint32_t *moves_out) {
    Board b;
    for (int i = 0; i < 12; i++) {
        b.pieces[i] = board_data[i];
    }
    b.flags = (uint32_t)board_data[12];

    return generate_legal_moves(&b, moves_out);
}

__attribute__((visibility("default")))
void engine_make_move(uint64_t *board_data, uint32_t move) {
    Board b;
    for (int i = 0; i < 12; i++) {
        b.pieces[i] = board_data[i];
    }
    b.flags = (uint32_t)board_data[12];

    Move m = move;
    make_move(&b, &m);

    for (int i = 0; i < 12; i++) {
        board_data[i] = b.pieces[i];
    }
    board_data[12] = b.flags;
}

__attribute__((visibility("default")))
uint32_t engine_search(uint64_t *board_data, int depth, int time_ms) {
    Board b;
    for (int i = 0; i < 12; i++) {
        b.pieces[i] = board_data[i];
    }
    b.flags = (uint32_t)board_data[12];

    return iterative_deepening(&b, depth, time_ms);
}

__attribute__((visibility("default")))
int engine_evaluate(uint64_t *board_data) {
    Board b;
    for (int i = 0; i < 12; i++) {
        b.pieces[i] = board_data[i];
    }
    b.flags = (uint32_t)board_data[12];

    return evaluate(&b);
}
