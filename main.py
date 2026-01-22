import os
import threading
import time

import berserk
import chess
from dotenv import load_dotenv

from engine_wrapper import engine, board_to_bitboards, move_to_uci
from http_server import ChallengeHandler, run_http_server


class ChessGame:
    def __init__(self, color: chess.Color, starting_fen: str):
        self.color = color
        self.board = chess.Board(starting_fen)
        self.max_depth = 20
        self.time_ms = 2000

    def half_moves(self):
        return self.board.ply()

    def push_uci(self, uci: str):
        move = self.board.parse_uci(uci)
        san = self.board.san(move)
        side = "White" if self.board.turn == chess.WHITE else "Black"
        move_num = self.board.fullmove_number
        self.board.push(move)
        print(f"[Move {move_num}] {side}: {san}")

    def my_turn(self):
        return self.board.turn == self.color

    def make_best_move(self) -> str:
        move_num = self.board.fullmove_number
        side = "White" if self.board.turn == chess.WHITE else "Black"
        print(f"\n[Move {move_num}] {side} thinking...")

        data = board_to_bitboards(self.board)
        move = engine.engine_search(data, self.max_depth, self.time_ms)
        uci = move_to_uci(move)

        chess_move = self.board.parse_uci(uci)
        san = self.board.san(chess_move)
        self.board.push(chess_move)

        print(f"[Move {move_num}] {side} plays: {san}\n")
        return uci


class LichessGame(threading.Thread):
    def __init__(self, client: berserk.Client, game_event, **kwargs):
        super().__init__(**kwargs)

        self.starting_fen = game_event['fen']
        self.game_id = game_event['gameId']
        self.client = client
        self.stream = client.bots.stream_game_state(self.game_id)
        self.opponent = game_event.get('opponent', {}).get('username', 'Unknown')
        self.start_time = time.time()

        if game_event['color'] == 'white':
            self.color = chess.WHITE
        else:
            self.color = chess.BLACK

        color_str = "White" if self.color == chess.WHITE else "Black"
        print("\n" + "=" * 50)
        print(f"  NEW GAME STARTED")
        print(f"  ID:       {self.game_id}")
        print(f"  Playing:  {color_str}")
        print(f"  Opponent: {self.opponent}")
        print("=" * 50 + "\n")

        self.chess_game = ChessGame(self.color, self.starting_fen)

    def resign(self):
        try:
            self.client.bots.resign_game(self.game_id)
            print(f"[Game {self.game_id}] Resigned due to timeout (1 hour)")
        except berserk.exceptions.ResponseError as e:
            print(f"[Error] Failed to resign game {self.game_id}: {e}")

    def run(self):
        for event in self.stream:
            if event['type'] == 'gameState':
                if not self.handle_state_change(event):
                    break
            elif event['type'] == 'chatLine':
                if not self.handle_chat_line(event):
                    break
            elif event['type'] == 'gameFull':
                if not self.handle_state_full(event):
                    break

    def handle_state_full(self, event):
        state = event.get('state')
        if state:
            return self.handle_state_change(state)
        return True

    def handle_state_change(self, game_state):
        status = game_state['status']
        if status != 'started' and status != 'created':
            print("\n" + "-" * 50)
            print(f"  GAME FINISHED: {status.upper()}")
            print("-" * 50 + "\n")
            return False

        moves_string = game_state['moves']
        move_list = moves_string.split() if moves_string else []

        processed_nr_moves = self.chess_game.half_moves()
        for i in range(processed_nr_moves, len(move_list)):
            self.chess_game.push_uci(move_list[i])

        if self.chess_game.my_turn():
            uci = self.chess_game.make_best_move()
            try:
                self.client.bots.make_move(self.game_id, uci)
            except berserk.exceptions.ResponseError as e:
                print(f"[Error] Failed to make move: {e}")

        return True

    def handle_chat_line(self, chat_line):
        return True


class Bot:
    def __init__(self, lichess_token: str, max_concurrent_games: int = 4):
        self.lichess_token = lichess_token
        self.current_games: dict[str, LichessGame] = {}
        self.max_concurrent_games = max_concurrent_games
        self.session = berserk.TokenSession(self.lichess_token)
        self.client = berserk.Client(self.session)
        self.current_challenge = None
        self.pending_challenge_id = None

    def check_game_timeouts(self):
        one_hour = 3600
        now = time.time()
        for game_id, game in list(self.current_games.items()):
            if now - game.start_time > one_hour:
                game.resign()

    def create_open_challenge(self):
        if self.pending_challenge_id:
            return

        try:
            response = self.client.challenges.create_open()
            self.current_challenge = response

            challenge_id = response.get('id')
            url_white = response.get('urlWhite')
            url_black = response.get('urlBlack')

            if challenge_id:
                self.pending_challenge_id = challenge_id
                self.client.bots.accept_challenge(challenge_id)

                print("\n" + "=" * 50)
                print("  NEW CHALLENGE CREATED!")
                print(f"  Challenge ID: {challenge_id}")
                print(f"  Join as White: {url_white}")
                print(f"  Join as Black: {url_black}")
                print("=" * 50 + "\n")

        except berserk.exceptions.ResponseError as e:
            print(f"[Error] Failed to create challenge: {e}")

    def challenge_loop(self):
        for event in self.client.bots.stream_incoming_events():
            if event['type'] == 'challenge':
                print(event)
                challenge = event['challenge']
                if len(self.current_games) < self.max_concurrent_games:
                    try:
                        self.client.bots.accept_challenge(challenge['id'])
                    except berserk.exceptions.ResponseError as e:
                        print(f"Failed to accept challenge: {e}")
                else:
                    print(f"Declining challenge - at max games ({self.max_concurrent_games})")
                    try:
                        self.client.bots.decline_challenge(challenge['id'])
                    except berserk.exceptions.ResponseError as e:
                        print(f"Failed to decline challenge: {e}")

            elif event['type'] == 'gameStart':
                game_event = event['game']
                game = LichessGame(self.client, game_event)
                self.current_games[game_event['gameId']] = game
                game.start()

                self.pending_challenge_id = None
                if len(self.current_games) < self.max_concurrent_games:
                    self.create_open_challenge()

            elif event['type'] == 'gameFinish':
                game_event = event['game']
                game_id = game_event['gameId']
                game = self.current_games.pop(game_id, None)
                if game:
                    game.join(timeout=5.0)

                if len(self.current_games) < self.max_concurrent_games:
                    self.create_open_challenge()


def run_timeout_checker(bot: Bot, interval: int = 60):
    while True:
        time.sleep(interval)
        bot.check_game_timeouts()


if __name__ == "__main__":
    load_dotenv()
    token = os.getenv("LICHESS_TOKEN")

    if not token:
        raise ValueError("LICHESS_TOKEN environment variable not set")

    bot = Bot(token)
    ChallengeHandler.bot = bot

    bot.create_open_challenge()

    http_thread = threading.Thread(target=run_http_server, daemon=True)
    http_thread.start()

    timeout_thread = threading.Thread(target=run_timeout_checker, args=(bot,), daemon=True)
    timeout_thread.start()

    bot.challenge_loop()
