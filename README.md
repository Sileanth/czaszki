# Szaszki

A Lichess chess bot with a custom C engine.

Source code: https://github.com/Sileanth/szaszki

## How to Play

### With a Lichess Account

Challenge the bot directly on Lichess: https://lichess.org/@/SileanthBOT

### Without a Lichess Account

Visit https://chess.sileanth.pl to get a challenge link. Click "Play as White" or "Play as Black" to start a game.

## Self-Hosting

### Requirements

- Docker and Docker Compose
- A Lichess account upgraded to a bot account
- A Lichess API token

### Setup

1. Create a Lichess account at https://lichess.org/signup

2. Upgrade your account to a bot account:
   - Go to https://lichess.org/account/oauth/token
   - Create a new token with `bot:play` scope
   - Use the Lichess API to upgrade your account:
     ```
     curl -X POST https://lichess.org/api/bot/account/upgrade \
       -H "Authorization: Bearer YOUR_TOKEN"
     ```
   - Note: This action is irreversible. The account can only be used as a bot after upgrading.

3. Create a `.env` file in the project root:
   ```
   LICHESS_TOKEN=your_lichess_api_token
   ```

4. Run the bot:
   ```
   docker compose up -d
   ```

The bot will start and listen on port 8080. It automatically creates open challenges and accepts incoming challenges.

### Configuration

The bot runs up to 4 concurrent games by default. Games running longer than 1 hour are automatically resigned.
