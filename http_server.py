from http.server import HTTPServer, BaseHTTPRequestHandler
from pathlib import Path

from jinja2 import Environment, FileSystemLoader

TEMPLATES_DIR = Path(__file__).parent / "templates"
jinja_env = Environment(loader=FileSystemLoader(TEMPLATES_DIR))


class ChallengeHandler(BaseHTTPRequestHandler):
    bot = None

    def log_message(self, format, *args):
        pass

    def do_GET(self):
        if self.path == '/':
            url_white = '#'
            url_black = '#'
            if ChallengeHandler.bot and ChallengeHandler.bot.current_challenge:
                url_white = ChallengeHandler.bot.current_challenge.get('urlWhite', '#')
                url_black = ChallengeHandler.bot.current_challenge.get('urlBlack', '#')

            template = jinja_env.get_template('index.html')
            html = template.render(url_white=url_white, url_black=url_black)

            self.send_response(200)
            self.send_header('Content-type', 'text/html')
            self.end_headers()
            self.wfile.write(html.encode())
        else:
            self.send_response(404)
            self.end_headers()


def run_http_server(port: int = 8080):
    server = HTTPServer(('', port), ChallengeHandler)
    print(f"HTTP server running on http://localhost:{port}")
    server.serve_forever()
