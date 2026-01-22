import ctypes
import pathlib

import chess

# Load the C chess engine
_libname = pathlib.Path(__file__).parent / "libchess.so"
engine = ctypes.CDLL(str(_libname))

# Set up function signatures
engine.engine_init.argtypes = []
engine.engine_init.restype = None

engine.engine_search.argtypes = [ctypes.POINTER(ctypes.c_uint64), ctypes.c_int, ctypes.c_int]
engine.engine_search.restype = ctypes.c_uint32

engine.engine_perft.argtypes = [ctypes.POINTER(ctypes.c_uint64), ctypes.c_int]
engine.engine_perft.restype = ctypes.c_uint64

engine.engine_get_legal_moves.argtypes = [ctypes.POINTER(ctypes.c_uint64), ctypes.POINTER(ctypes.c_uint32)]
engine.engine_get_legal_moves.restype = ctypes.c_int

engine.engine_init()

# Piece indices matching C code
WP, WN, WB, WR, WQ, WK = 0, 1, 2, 3, 4, 5
BP, BN, BB, BR, BQ, BK = 6, 7, 8, 9, 10, 11

# Castling bits matching C engine
CASTLE_WK = 1
CASTLE_WQ = 2
CASTLE_BK = 4
CASTLE_BQ = 8


def board_to_bitboards(board: chess.Board) -> ctypes.Array:
    data = (ctypes.c_uint64 * 13)()

    data[WP] = int(board.pieces(chess.PAWN, chess.WHITE))
    data[WN] = int(board.pieces(chess.KNIGHT, chess.WHITE))
    data[WB] = int(board.pieces(chess.BISHOP, chess.WHITE))
    data[WR] = int(board.pieces(chess.ROOK, chess.WHITE))
    data[WQ] = int(board.pieces(chess.QUEEN, chess.WHITE))
    data[WK] = int(board.pieces(chess.KING, chess.WHITE))
    data[BP] = int(board.pieces(chess.PAWN, chess.BLACK))
    data[BN] = int(board.pieces(chess.KNIGHT, chess.BLACK))
    data[BB] = int(board.pieces(chess.BISHOP, chess.BLACK))
    data[BR] = int(board.pieces(chess.ROOK, chess.BLACK))
    data[BQ] = int(board.pieces(chess.QUEEN, chess.BLACK))
    data[BK] = int(board.pieces(chess.KING, chess.BLACK))

    flags = 0
    if board.turn == chess.BLACK:
        flags |= 1

    castling = 0
    if board.has_kingside_castling_rights(chess.WHITE):
        castling |= CASTLE_WK
    if board.has_queenside_castling_rights(chess.WHITE):
        castling |= CASTLE_WQ
    if board.has_kingside_castling_rights(chess.BLACK):
        castling |= CASTLE_BK
    if board.has_queenside_castling_rights(chess.BLACK):
        castling |= CASTLE_BQ
    flags |= (castling << 1)

    if board.ep_square is not None:
        ep_file = chess.square_file(board.ep_square)
    else:
        ep_file = 15
    flags |= (ep_file << 5)

    data[12] = flags
    return data


def move_to_uci(move: int) -> str:
    from_sq = move & 0x3F
    to_sq = (move >> 6) & 0x3F
    flags = (move >> 12) & 0xF

    from_str = chess.square_name(from_sq)
    to_str = chess.square_name(to_sq)

    promo = ""
    if flags >= 8 and flags <= 11:
        promo = "nbrq"[flags - 8]
    elif flags >= 12:
        promo = "nbrq"[flags - 12]

    return from_str + to_str + promo
