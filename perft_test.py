import ctypes

import chess

from engine_wrapper import engine, board_to_bitboards, move_to_uci


def perft_c(board: chess.Board, depth: int) -> int:
    data = board_to_bitboards(board)
    return engine.engine_perft(data, depth)


def perft_python(board: chess.Board, depth: int) -> int:
    if depth == 0:
        return 1

    moves = list(board.legal_moves)
    if depth == 1:
        return len(moves)

    nodes = 0
    for move in moves:
        board.push(move)
        nodes += perft_python(board, depth - 1)
        board.pop()
    return nodes


# Known perft results for testing
# https://www.chessprogramming.org/Perft_Results
PERFT_TESTS = [
    # Starting position
    ("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", [
        (1, 20),
        (2, 400),
        (3, 8902),
        (4, 197281),
        (5, 4865609),
    ]),
    # Position 2 (Kiwipete)
    ("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", [
        (1, 48),
        (2, 2039),
        (3, 97862),
        (4, 4085603),
    ]),
    # Position 3
    ("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", [
        (1, 14),
        (2, 191),
        (3, 2812),
        (4, 43238),
        (5, 674624),
    ]),
    # Position 4
    ("r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", [
        (1, 6),
        (2, 264),
        (3, 9467),
        (4, 422333),
    ]),
    # Position 5
    ("rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", [
        (1, 44),
        (2, 1486),
        (3, 62379),
        (4, 2103487),
    ]),
    # Position 6
    ("r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", [
        (1, 46),
        (2, 2079),
        (3, 89890),
        (4, 3894594),
    ]),
]


def run_perft_tests(max_depth: int = 4):
    print("Running perft tests...\n")

    all_passed = True

    for fen, expected_results in PERFT_TESTS:
        print(f"Position: {fen}")
        board = chess.Board(fen)

        for depth, expected in expected_results:
            if depth > max_depth:
                continue

            result = perft_c(board, depth)
            status = "OK" if result == expected else "FAIL"

            if result != expected:
                all_passed = False
                py_result = perft_python(board, depth)
                print(f"  Depth {depth}: {result} (expected {expected}) [{status}]")
                print(f"    python-chess: {py_result}")
            else:
                print(f"  Depth {depth}: {result} [{status}]")

        print()

    if all_passed:
        print("All tests passed!")
    else:
        print("Some tests FAILED!")

    return all_passed


# Show perft breakdown by first move
def perft_divide(board: chess.Board, depth: int):
    moves_out = (ctypes.c_uint32 * 256)()
    data = board_to_bitboards(board)
    count = engine.engine_get_legal_moves(data, moves_out)

    total = 0
    for i in range(count):
        move = moves_out[i]
        uci = move_to_uci(move)

        board.push_uci(uci)
        nodes = perft_c(board, depth - 1) if depth > 1 else 1
        board.pop()

        print(f"{uci}: {nodes}")
        total += nodes

    print(f"\nTotal: {total}")


if __name__ == "__main__":
    import sys
    import time

    if len(sys.argv) > 1 and sys.argv[1] == "divide":
        depth = int(sys.argv[2]) if len(sys.argv) > 2 else 4
        fen = sys.argv[3] if len(sys.argv) > 3 else chess.STARTING_FEN
        board = chess.Board(fen)
        perft_divide(board, depth)
    else:
        max_depth = int(sys.argv[1]) if len(sys.argv) > 1 else 4

        # Run tests
        run_perft_tests(max_depth)

        # Benchmark
        print("\nBenchmark (starting position):")
        board = chess.Board()
        for d in range(1, min(max_depth + 1, 7)):
            start = time.time()
            nodes = perft_c(board, d)
            elapsed = time.time() - start
            nps = nodes / elapsed if elapsed > 0 else 0
            print(f"  Depth {d}: {nodes:>12} nodes in {elapsed:.3f}s ({nps/1e6:.2f}M nps)")
