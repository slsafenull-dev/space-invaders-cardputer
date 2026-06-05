/*
 * ============================================================
 *  SPACE INVADERS  for M5Stack Cardputer
 *  Board  : M5Stack Cardputer (ESP32-S3)
 *  Library: M5Unified / M5GFX / M5Cardputer
 * ============================================================
 *  キーボード API:
 *    #include <M5Cardputer.h>
 *    M5Cardputer.update();
 *    M5Cardputer.Keyboard.isKeyPressed('x');
 * ============================================================
 */

#include <M5Cardputer.h>   // Cardputer専用(Keyboard APIを含む)
#include <M5GFX.h>

// ─── 画面定数 ────────────────────────────────────────────────
static const int SCREEN_W   = 240;
static const int SCREEN_H   = 135;
static const int STATUS_H   = 14;
static const int PLAY_Y     = STATUS_H;

// ─── ゲーム定数 ─────────────────────────────────────────────
static const int PLAYER_W   = 12;
static const int PLAYER_H   =  8;
static const int PLAYER_SPD =  3;
static const int MAX_LIVES  =  3;

static const int INV_COLS   =  9;
static const int INV_ROWS   =  4;
static const int INV_W      = 12;
static const int INV_H      =  8;
static const int INV_PAD_X  =  4;
static const int INV_PAD_Y  =  4;
static const int INV_OFS_X  = 10;
static const int INV_OFS_Y  =  6;

static const int MAX_PBULLETS = 3;
static const int MAX_EBULLETS = 5;
static const int PBULLET_SPD  = 5;
static const int EBULLET_SPD  = 2;

static const int INV_DROP   =  6;
static const int INV_STEP_X =  2;

// ─── カラー ─────────────────────────────────────────────────
static const uint16_t C_BLACK   = 0x0000;
static const uint16_t C_WHITE   = 0xFFFF;
static const uint16_t C_GREEN   = 0x07E0;
static const uint16_t C_RED     = 0xF800;
static const uint16_t C_YELLOW  = 0xFFE0;
static const uint16_t C_CYAN    = 0x07FF;
static const uint16_t C_MAGENTA = 0xF81F;
static const uint16_t C_DKGRAY  = 0x2104;
static const uint16_t C_ORANGE  = 0xFC00;

// ─── ゲームステート ──────────────────────────────────────────
enum GameState { STATE_TITLE, STATE_PLAY, STATE_OVER, STATE_CLEAR };

// ─── 構造体 ─────────────────────────────────────────────────
struct Bullet {
  float x, y;
  bool  active;
};

struct Invader {
  float x, y;
  bool  alive;
  uint8_t type;   // 0=下段 1=中段 2=上段
};

// ─── グローバル変数 ──────────────────────────────────────────
static GameState   gState;
static M5GFX&      gfx    = M5Cardputer.Display;
static LGFX_Sprite sprite;

// Player
static float px, py;
static int   pLives;
static bool  pHit;
static int   pHitTimer;

// Bullets
static Bullet pBullets[MAX_PBULLETS];
static Bullet eBullets[MAX_EBULLETS];

// Invaders
static Invader invaders[INV_ROWS * INV_COLS];
static float   invDX;
static float   invSpeedMul;
static bool    invNeedDrop;
static int     invMoveTimer;
static int     invMoveInterval;
static int     invAliveCount;

// Score
static int gScore;
static int gHiScore;

// BGM
static uint32_t bgmTimer;
static int      bgmStep;

// タイトル点滅
static uint32_t blinkTimer;
static bool     blinkOn;

// ─── BGM パターン ────────────────────────────────────────────
static const int BGM_LEN = 16;
static const int BGM_NOTE[BGM_LEN] = {
  220,0,220,0,262,0,262,0,
  294,0,330,0,294,0,220,0
};
static const int BGM_DUR = 120;

// ─── プロトタイプ ────────────────────────────────────────────
void initPlay();
void updatePlay();
void drawPlay();
void drawTitle();
void drawGameOver();
void drawClear();
void firePBullet();
void fireEBullet();
void updateBGM();
void playSFX(int freq, int dur);
void drawInvader(LGFX_Sprite& s, int x, int y, uint8_t type, uint16_t col);
void drawPlayer(LGFX_Sprite& s, int x, int y, uint16_t col);

// ─── SETUP ───────────────────────────────────────────────────
void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);   // true = Keyboardも初期化

  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.fillScreen(C_BLACK);

  sprite.createSprite(SCREEN_W, SCREEN_H);

  gHiScore   = 0;
  gState     = STATE_TITLE;
  bgmStep    = 0;
  bgmTimer   = 0;
  blinkTimer = 0;
  blinkOn    = true;
}

// ─── LOOP ────────────────────────────────────────────────────
void loop() {
  M5Cardputer.update();           // ← Keyboard読み込みはここで一括更新
  uint32_t now = millis();

  if (gState == STATE_PLAY) updateBGM();

  // ── タイトル ──
  if (gState == STATE_TITLE) {
    if (now - blinkTimer > 500) { blinkOn = !blinkOn; blinkTimer = now; }
    drawTitle();
    if (M5Cardputer.Keyboard.isKeyPressed(' ')) {
      delay(200);
      initPlay();
      gState = STATE_PLAY;
    }
    delay(16);
    return;
  }

  // ── ゲームオーバー / クリア ──
  if (gState == STATE_OVER || gState == STATE_CLEAR) {
    if (now - blinkTimer > 500) { blinkOn = !blinkOn; blinkTimer = now; }
    if (gState == STATE_OVER) drawGameOver();
    else                      drawClear();
    if (M5Cardputer.Keyboard.isKeyPressed(' ')) {
      delay(200);
      gState = STATE_TITLE;
    }
    delay(16);
    return;
  }

  // ── プレイ ──
  updatePlay();
  drawPlay();
  delay(16);
}

// ─── ゲーム初期化 ────────────────────────────────────────────
void initPlay() {
  px     = SCREEN_W / 2.0f - PLAYER_W / 2.0f;
  py     = SCREEN_H - PLAYER_H - 4.0f;
  pLives = MAX_LIVES;
  pHit   = false;
  pHitTimer = 0;
  gScore = 0;

  for (int i = 0; i < MAX_PBULLETS; i++) pBullets[i].active = false;
  for (int i = 0; i < MAX_EBULLETS; i++) eBullets[i].active = false;

  int idx = 0;
  for (int r = 0; r < INV_ROWS; r++) {
    for (int c = 0; c < INV_COLS; c++) {
      float ix = INV_OFS_X + c * (INV_W + INV_PAD_X);
      float iy = PLAY_Y + INV_OFS_Y + r * (INV_H + INV_PAD_Y);
      invaders[idx].x     = ix;
      invaders[idx].y     = iy;
      invaders[idx].alive = true;
      // 上段ほど高得点タイプ
      invaders[idx].type  = (r == 0) ? 2 : (r == 1) ? 1 : 0;
      idx++;
    }
  }

  invDX           =  1.0f;
  invSpeedMul     =  1.0f;
  invNeedDrop     =  false;
  invMoveInterval =  6;
  invMoveTimer    =  0;
  invAliveCount   =  INV_ROWS * INV_COLS;

  bgmStep  = 0;
  bgmTimer = millis();
}

// ─── 更新処理 ────────────────────────────────────────────────
void updatePlay() {

  // ── キー入力 ──────────────────────────────────────────────
  // Cardputer物理キー: 左=';'列 or ','、右='.'or'/'、発射=' '(スペース)
  // isKeyPressed は char または uint8_t キーコードを受け取る
  bool keyLeft  = M5Cardputer.Keyboard.isKeyPressed('k');
  bool keyRight = M5Cardputer.Keyboard.isKeyPressed('l');
  bool keyShot  = M5Cardputer.Keyboard.isKeyPressed('a');

  if (!pHit) {
    if (keyLeft)  { px -= PLAYER_SPD; if (px < 0) px = 0; }
    if (keyRight) { px += PLAYER_SPD; if (px > SCREEN_W - PLAYER_W) px = SCREEN_W - PLAYER_W; }

    static bool prevShot = false;
    if (keyShot && !prevShot) firePBullet();
    prevShot = keyShot;
  }

  // ── 被弾フラッシュ ────────────────────────────────────────
  if (pHit) {
    pHitTimer--;
    if (pHitTimer <= 0) pHit = false;
  }

  // ── プレイヤー弾移動 ──────────────────────────────────────
  for (int i = 0; i < MAX_PBULLETS; i++) {
    if (!pBullets[i].active) continue;
    pBullets[i].y -= PBULLET_SPD;
    if (pBullets[i].y < PLAY_Y) pBullets[i].active = false;
  }

  // ── インベーダー移動 ──────────────────────────────────────
  invMoveTimer++;
  if (invMoveTimer >= invMoveInterval) {
    invMoveTimer = 0;
    if (invNeedDrop) {
      for (int i = 0; i < INV_ROWS * INV_COLS; i++) {
        if (invaders[i].alive) invaders[i].y += INV_DROP;
      }
      invDX       = -invDX;
      invNeedDrop = false;
    } else {
      float step    = INV_STEP_X * invDX * invSpeedMul;
      bool  hitWall = false;
      for (int i = 0; i < INV_ROWS * INV_COLS; i++) {
        if (!invaders[i].alive) continue;
        invaders[i].x += step;
        if (invaders[i].x <= 0 || invaders[i].x >= SCREEN_W - INV_W) hitWall = true;
      }
      if (hitWall) invNeedDrop = true;
    }
  }

  // ── インベーダーが最下端に到達 → ゲームオーバー ──────────
  for (int i = 0; i < INV_ROWS * INV_COLS; i++) {
    if (!invaders[i].alive) continue;
    if (invaders[i].y + INV_H >= py) {
      gState = STATE_OVER;
      playSFX(100, 500);
      return;
    }
  }

  // ── 敵弾発射 ─────────────────────────────────────────────
  if (random(100) < 3) fireEBullet();

  // ── 敵弾移動 ─────────────────────────────────────────────
  for (int i = 0; i < MAX_EBULLETS; i++) {
    if (!eBullets[i].active) continue;
    eBullets[i].y += EBULLET_SPD;
    if (eBullets[i].y > SCREEN_H) eBullets[i].active = false;
  }

  // ── 衝突: プレイヤー弾 vs インベーダー ───────────────────
  for (int b = 0; b < MAX_PBULLETS; b++) {
    if (!pBullets[b].active) continue;
    for (int i = 0; i < INV_ROWS * INV_COLS; i++) {
      if (!invaders[i].alive) continue;
      if (pBullets[b].x >= invaders[i].x     &&
          pBullets[b].x <= invaders[i].x + INV_W &&
          pBullets[b].y >= invaders[i].y     &&
          pBullets[b].y <= invaders[i].y + INV_H) {
        invaders[i].alive  = false;
        pBullets[b].active = false;
        int pts = (invaders[i].type == 2) ? 30 : (invaders[i].type == 1) ? 20 : 10;
        gScore += pts;
        invAliveCount--;
        invSpeedMul     = 1.0f + (float)(INV_ROWS * INV_COLS - invAliveCount)
                                       / (INV_ROWS * INV_COLS) * 2.5f;
        invMoveInterval = max(1, 6 - (int)(invAliveCount / 6));
        playSFX(800 + pts * 3, 60);
        break;
      }
    }
  }

  // ── 全滅チェック ─────────────────────────────────────────
  if (invAliveCount <= 0) {
    if (gScore > gHiScore) gHiScore = gScore;
    gState = STATE_CLEAR;
    playSFX(1047, 400);
    return;
  }

  // ── 衝突: 敵弾 vs プレイヤー ─────────────────────────────
  if (!pHit) {
    for (int i = 0; i < MAX_EBULLETS; i++) {
      if (!eBullets[i].active) continue;
      if (eBullets[i].x >= px          &&
          eBullets[i].x <= px + PLAYER_W &&
          eBullets[i].y >= py          &&
          eBullets[i].y <= py + PLAYER_H) {
        eBullets[i].active = false;
        pLives--;
        pHit      = true;
        pHitTimer = 60;
        playSFX(200, 300);
        if (pLives <= 0) {
          if (gScore > gHiScore) gHiScore = gScore;
          gState = STATE_OVER;
          playSFX(100, 600);
          return;
        }
      }
    }
  }
}

// ─── 描画 ────────────────────────────────────────────────────
void drawPlay() {
  sprite.fillScreen(C_BLACK);

  // ステータスバー
  sprite.fillRect(0, 0, SCREEN_W, STATUS_H, C_DKGRAY);
  sprite.setTextSize(1);
  sprite.setTextColor(C_WHITE);
  sprite.setCursor(2, 3);
  sprite.printf("SCORE:%05d", gScore);
  sprite.setCursor(90, 3);
  sprite.printf("HI:%05d", gHiScore);
  sprite.setCursor(180, 3);
  sprite.print("LIFE:");
  for (int i = 0; i < pLives; i++) drawPlayer(sprite, 210 + i * 9, 2, C_GREEN);
  sprite.drawFastHLine(0, STATUS_H, SCREEN_W, C_DKGRAY);

  // プレイヤー
  uint16_t pcol = (pHit && (pHitTimer % 6 < 3)) ? C_RED : C_GREEN;
  drawPlayer(sprite, (int)px, (int)py, pcol);

  // プレイヤー弾
  for (int i = 0; i < MAX_PBULLETS; i++) {
    if (!pBullets[i].active) continue;
    sprite.fillRect((int)pBullets[i].x, (int)pBullets[i].y, 2, 5, C_YELLOW);
  }

  // インベーダー
  for (int i = 0; i < INV_ROWS * INV_COLS; i++) {
    if (!invaders[i].alive) continue;
    uint16_t col = (invaders[i].type == 2) ? C_CYAN :
                   (invaders[i].type == 1) ? C_MAGENTA : C_GREEN;
    drawInvader(sprite, (int)invaders[i].x, (int)invaders[i].y, invaders[i].type, col);
  }

  // 敵弾
  for (int i = 0; i < MAX_EBULLETS; i++) {
    if (!eBullets[i].active) continue;
    sprite.fillRect((int)eBullets[i].x, (int)eBullets[i].y, 2, 6, C_ORANGE);
  }

  sprite.pushSprite(&M5Cardputer.Display, 0, 0);
}

// ─── タイトル ────────────────────────────────────────────────
void drawTitle() {
  sprite.fillScreen(C_BLACK);
  for (int i = 0; i < 40; i++) {
    sprite.drawPixel((i * 73 + 11) % SCREEN_W, (i * 47 + 5) % SCREEN_H, C_WHITE);
  }
  sprite.setTextSize(2);
  sprite.setTextColor(C_GREEN);
  sprite.setCursor(28, 18);
  sprite.print("SPACE INVADERS");

  for (int c = 0; c < 5; c++) {
    uint16_t col = (c % 3 == 0) ? C_CYAN : (c % 3 == 1) ? C_MAGENTA : C_GREEN;
    drawInvader(sprite, 20 + c * 40, 48, c % 3, col);
  }

  if (blinkOn) {
    sprite.setTextSize(1);
    sprite.setTextColor(C_YELLOW);
    sprite.setCursor(58, 75);
    sprite.print("PRESS SPACE TO START");
  }
  sprite.setTextSize(1);
  sprite.setTextColor(C_WHITE);
  sprite.setCursor(70, 92);
  sprite.printf("HI-SCORE: %05d", gHiScore);

  sprite.setTextColor(C_DKGRAY);
  sprite.setCursor(34, 108);
  sprite.print("k=LEFT   l=RIGHT   a=FIRE");

  sprite.pushSprite(&M5Cardputer.Display, 0, 0);
}

// ─── ゲームオーバー ──────────────────────────────────────────
void drawGameOver() {
  sprite.fillScreen(C_BLACK);
  sprite.setTextSize(2);
  sprite.setTextColor(C_RED);
  sprite.setCursor(44, 30);
  sprite.print("GAME OVER");

  sprite.setTextSize(1);
  sprite.setTextColor(C_WHITE);
  sprite.setCursor(60, 62);
  sprite.printf("SCORE: %05d", gScore);
  sprite.setCursor(60, 78);
  sprite.printf("BEST:  %05d", gHiScore);

  if (blinkOn) {
    sprite.setTextColor(C_YELLOW);
    sprite.setCursor(55, 105);
    sprite.print("PRESS SPACE TO RETRY");
  }
  sprite.pushSprite(&M5Cardputer.Display, 0, 0);
}

// ─── クリア ──────────────────────────────────────────────────
void drawClear() {
  sprite.fillScreen(C_BLACK);
  sprite.setTextSize(2);
  sprite.setTextColor(C_YELLOW);
  sprite.setCursor(40, 20);
  sprite.print("YOU WIN !!");

  sprite.setTextSize(1);
  sprite.setTextColor(C_GREEN);
  sprite.setCursor(60, 58);
  sprite.printf("SCORE: %05d", gScore);
  sprite.setCursor(60, 74);
  sprite.printf("BEST:  %05d", gHiScore);

  for (int i = 0; i < 20; i++) {
    int fx = (i * 37 + millis() / 100) % SCREEN_W;
    int fy = (i * 29 + millis() / 80)  % (SCREEN_H / 2) + SCREEN_H / 4;
    uint16_t cc = (i%4==0)?C_YELLOW:(i%4==1)?C_CYAN:(i%4==2)?C_MAGENTA:C_GREEN;
    sprite.fillCircle(fx, fy, 2, cc);
  }

  if (blinkOn) {
    sprite.setTextColor(C_WHITE);
    sprite.setCursor(55, 105);
    sprite.print("PRESS SPACE TO TITLE");
  }
  sprite.pushSprite(&M5Cardputer.Display, 0, 0);
}

// ─── プレイヤー弾発射 ────────────────────────────────────────
void firePBullet() {
  for (int i = 0; i < MAX_PBULLETS; i++) {
    if (!pBullets[i].active) {
      pBullets[i].x      = px + PLAYER_W / 2.0f - 1;
      pBullets[i].y      = py - 5;
      pBullets[i].active = true;
      playSFX(1200, 40);
      return;
    }
  }
}

// ─── 敵弾発射 ────────────────────────────────────────────────
void fireEBullet() {
  int slot = -1;
  for (int i = 0; i < MAX_EBULLETS; i++) {
    if (!eBullets[i].active) { slot = i; break; }
  }
  if (slot < 0) return;

  for (int attempt = 0; attempt < 20; attempt++) {
    int idx = random(INV_ROWS * INV_COLS);
    if (invaders[idx].alive) {
      eBullets[slot].x      = invaders[idx].x + INV_W / 2.0f - 1;
      eBullets[slot].y      = invaders[idx].y + INV_H;
      eBullets[slot].active = true;
      return;
    }
  }
}

// ─── BGM ─────────────────────────────────────────────────────
void updateBGM() {
  uint32_t now = millis();
  if (now - bgmTimer > (uint32_t)BGM_DUR) {
    bgmTimer = now;
    int freq = BGM_NOTE[bgmStep % BGM_LEN];
    if (freq > 0) M5Cardputer.Speaker.tone(freq, BGM_DUR - 10);
    bgmStep++;
  }
}

// ─── 効果音 ──────────────────────────────────────────────────
void playSFX(int freq, int dur) {
  M5Cardputer.Speaker.tone(freq, dur);
}

// ─── インベーダー描画 ────────────────────────────────────────
void drawInvader(LGFX_Sprite& s, int x, int y, uint8_t type, uint16_t col) {
  if (type == 0) {
    s.fillRect(x+2, y,   8, 2, col);
    s.fillRect(x,   y+2, 12,2, col);
    s.fillRect(x+2, y+4, 3, 2, col);
    s.fillRect(x+7, y+4, 3, 2, col);
    s.fillRect(x,   y+6, 4, 2, col);
    s.fillRect(x+8, y+6, 4, 2, col);
  } else if (type == 1) {
    s.fillRect(x+4, y,   4, 2, col);
    s.fillRect(x+2, y+2, 8, 2, col);
    s.fillRect(x,   y+4, 12,2, col);
    s.fillRect(x+2, y+6, 3, 2, col);
    s.fillRect(x+7, y+6, 3, 2, col);
  } else {
    s.fillRect(x+3, y,   6, 2, col);
    s.fillRect(x,   y+2, 12,4, col);
    s.fillRect(x+2, y+6, 2, 2, col);
    s.fillRect(x+8, y+6, 2, 2, col);
  }
}

// ─── プレイヤー描画 ──────────────────────────────────────────
void drawPlayer(LGFX_Sprite& s, int x, int y, uint16_t col) {
  s.fillRect(x+5, y,   2, 2, col);
  s.fillRect(x+4, y+2, 4, 2, col);
  s.fillRect(x+3, y+4, 6, 2, col);
  s.fillRect(x,   y+6, 12,2, col);
}
