#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <conio.h>
#include <windows.h>
#include <time.h>

#define MAP_W 50
#define MAP_H 20
#define MAX_MINIONS 50
#define TICK_MS 120
#define SPAWN_INTERVAL 20
#define XP_PER_MINION 10
#define HERO_ATTACK_COOLDOWN 8
#define MINION_ATTACK_COOLDOWN 10
#define TOWER_ATTACK_COOLDOWN 12

typedef struct {
    int x, y;
    int hp, maxHp;
    int attack;
    int attackRange;
    int level;
    int xp;
    int nextLevelXp;
    int alive;
    int dirX, dirY;
    int attackCooldown;
    int lastAttackTick;
    int attackRequested;
    int kills;
} Hero;

typedef struct {
    int x, y;
    int hp, maxHp;
    int attack;
    int alive;
    int lastHitByPlayer;
    int lastAttackTick;
} Minion;

typedef struct {
    int x, y;
    int hp, maxHp;
    int attack;
    int alive;
    int team; //0 player,1 enemy
    int lastAttackTick;
} Tower;

static char gMap[MAP_H][MAP_W + 1];
static Hero gHero;
static Minion gMinions[MAX_MINIONS];
static Tower gTowers[2];
static int gRunning = 1;
static int gTick = 0;
static HANDLE gConsole;
pthread_mutex_t gStateMutex = PTHREAD_MUTEX_INITIALIZER;

static void initMap(void) {
    for (int y = 0; y < MAP_H; ++y) {
        for (int x = 0; x < MAP_W; ++x) {
            if (y == 0 || y == MAP_H - 1 || x == 0 || x == MAP_W - 1) {
                gMap[y][x] = '#';
            } else {
                gMap[y][x] = ' ';
            }
        }
        gMap[y][MAP_W] = '\0';
    }
    // simple obstacles
    for (int x = 12; x < 18; ++x) {
        gMap[8][x] = 'X';
    }
    for (int x = 30; x < 36; ++x) {
        gMap[12][x] = 'X';
    }
}

static void initHero(void) {
    gHero.x = 4;
    gHero.y = MAP_H / 2;
    gHero.maxHp = 120;
    gHero.hp = gHero.maxHp;
    gHero.attack = 15;
    gHero.attackRange = 3;
    gHero.level = 1;
    gHero.xp = 0;
    gHero.nextLevelXp = 50;
    gHero.alive = 1;
    gHero.dirX = 0;
    gHero.dirY = 0;
    gHero.attackCooldown = HERO_ATTACK_COOLDOWN;
    gHero.lastAttackTick = -HERO_ATTACK_COOLDOWN;
    gHero.attackRequested = 0;
    gHero.kills = 0;
}

static void initTowers(void) {
    // player tower left
    gTowers[0].x = 2;
    gTowers[0].y = MAP_H / 2;
    gTowers[0].maxHp = 300;
    gTowers[0].hp = gTowers[0].maxHp;
    gTowers[0].attack = 18;
    gTowers[0].alive = 1;
    gTowers[0].team = 0;
    gTowers[0].lastAttackTick = -TOWER_ATTACK_COOLDOWN;

    // enemy tower right
    gTowers[1].x = MAP_W - 3;
    gTowers[1].y = MAP_H / 2;
    gTowers[1].maxHp = 320;
    gTowers[1].hp = gTowers[1].maxHp;
    gTowers[1].attack = 20;
    gTowers[1].alive = 1;
    gTowers[1].team = 1;
    gTowers[1].lastAttackTick = -TOWER_ATTACK_COOLDOWN;
}

static void initMinions(void) {
    for (int i = 0; i < MAX_MINIONS; ++i) {
        gMinions[i].alive = 0;
    }
}

static int isWalkable(int x, int y) {
    if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H) return 0;
    char c = gMap[y][x];
    return c != '#' && c != 'X';
}

static int distanceManhattan(int x1, int y1, int x2, int y2) {
    return abs(x1 - x2) + abs(y1 - y2);
}

static void levelUp(void) {
    gHero.level++;
    gHero.maxHp += 20;
    gHero.attack += 3;
    gHero.hp = gHero.maxHp;
    gHero.nextLevelXp = (int)(gHero.nextLevelXp * 1.5);
}

static void spawnMinionWave(void) {
    int spawnCount = 1 + rand() % 3;
    for (int i = 0; i < MAX_MINIONS && spawnCount > 0; ++i) {
        if (!gMinions[i].alive) {
            gMinions[i].alive = 1;
            gMinions[i].x = MAP_W - 5;
            gMinions[i].y = 2 + rand() % (MAP_H - 4);
            gMinions[i].maxHp = 50;
            gMinions[i].hp = gMinions[i].maxHp;
            gMinions[i].attack = 6;
            gMinions[i].lastHitByPlayer = 0;
            gMinions[i].lastAttackTick = -MINION_ATTACK_COOLDOWN;
            spawnCount--;
        }
    }
}

static void handleHeroAttack(void) {
    int bestIdx = -1;
    int bestDist = 9999;
    // target minion first
    for (int i = 0; i < MAX_MINIONS; ++i) {
        if (!gMinions[i].alive) continue;
        int d = distanceManhattan(gHero.x, gHero.y, gMinions[i].x, gMinions[i].y);
        if (d <= gHero.attackRange && d < bestDist) {
            bestDist = d;
            bestIdx = i;
        }
    }
    if (bestIdx >= 0) {
        gMinions[bestIdx].hp -= gHero.attack;
        gMinions[bestIdx].lastHitByPlayer = 1;
        gHero.lastAttackTick = gTick;
        gHero.attackRequested = 0;
        return;
    }
    // try enemy tower
    Tower *enemyTower = &gTowers[1];
    if (enemyTower->alive) {
        int d = distanceManhattan(gHero.x, gHero.y, enemyTower->x, enemyTower->y);
        if (d <= gHero.attackRange) {
            enemyTower->hp -= gHero.attack;
            if (enemyTower->hp <= 0) {
                enemyTower->hp = 0;
                enemyTower->alive = 0;
            }
            gHero.lastAttackTick = gTick;
        }
    }
    gHero.attackRequested = 0;
}

static void updateHero(void) {
    // movement
    int nx = gHero.x + gHero.dirX;
    int ny = gHero.y + gHero.dirY;
    if (isWalkable(nx, ny)) {
        gHero.x = nx;
        gHero.y = ny;
    }
    // attack
    if (gHero.attackRequested && (gTick - gHero.lastAttackTick >= gHero.attackCooldown)) {
        handleHeroAttack();
    }
}

static void minionAttackHero(Minion *m) {
    if (!m->alive || !gHero.alive) return;
    if (distanceManhattan(m->x, m->y, gHero.x, gHero.y) <= 1) {
        if (gTick - m->lastAttackTick >= MINION_ATTACK_COOLDOWN) {
            gHero.hp -= m->attack;
            if (gHero.hp <= 0) {
                gHero.hp = 0;
                gHero.alive = 0;
            }
            m->lastAttackTick = gTick;
        }
    }
}

static void minionAttackTower(Minion *m, Tower *tower) {
    if (!m->alive || !tower->alive) return;
    if (distanceManhattan(m->x, m->y, tower->x, tower->y) <= 1) {
        if (gTick - m->lastAttackTick >= MINION_ATTACK_COOLDOWN) {
            tower->hp -= m->attack;
            if (tower->hp <= 0) {
                tower->hp = 0;
                tower->alive = 0;
            }
            m->lastAttackTick = gTick;
        }
    }
}

static void updateMinions(void) {
    for (int i = 0; i < MAX_MINIONS; ++i) {
        if (!gMinions[i].alive) continue;
        // move towards left (player side)
        int dx = (gMinions[i].x > 3) ? -1 : 0;
        int nx = gMinions[i].x + dx;
        if (isWalkable(nx, gMinions[i].y)) {
            gMinions[i].x = nx;
        }
        // attack hero or player tower
        minionAttackHero(&gMinions[i]);
        minionAttackTower(&gMinions[i], &gTowers[0]);
    }
}

static void updateTowers(void) {
    for (int i = 0; i < 2; ++i) {
        Tower *t = &gTowers[i];
        if (!t->alive) continue;
        if (gTick - t->lastAttackTick < TOWER_ATTACK_COOLDOWN) continue;
        int bestDist = 9999;
        int targetMinion = -1;
        for (int m = 0; m < MAX_MINIONS; ++m) {
            if (!gMinions[m].alive) continue;
            int isEnemy = (t->team == 0); // player tower targets enemy minions
            if (!isEnemy) break; // enemy tower doesn't attack minions, only hero
            int d = distanceManhattan(t->x, t->y, gMinions[m].x, gMinions[m].y);
            if (d < bestDist) {
                bestDist = d;
                targetMinion = m;
            }
        }
        if (t->team == 1 && gHero.alive) {
            int dHero = distanceManhattan(t->x, t->y, gHero.x, gHero.y);
            if (dHero <= 5) {
                gHero.hp -= t->attack;
                if (gHero.hp <= 0) {
                    gHero.hp = 0;
                    gHero.alive = 0;
                }
                t->lastAttackTick = gTick;
                continue;
            }
        }
        if (t->team == 0 && targetMinion >= 0 && bestDist <= 5) {
            gMinions[targetMinion].hp -= t->attack;
            t->lastAttackTick = gTick;
        }
    }
}

static void handleDeathsAndXP(void) {
    for (int i = 0; i < MAX_MINIONS; ++i) {
        if (!gMinions[i].alive) continue;
        if (gMinions[i].hp <= 0) {
            if (gMinions[i].lastHitByPlayer) {
                gHero.xp += XP_PER_MINION;
                gHero.kills++;
                while (gHero.xp >= gHero.nextLevelXp) {
                    gHero.xp -= gHero.nextLevelXp;
                    levelUp();
                }
            }
            gMinions[i].alive = 0;
        }
    }
}

static int checkVictory(void) {
    if (!gHero.alive || !gTowers[0].alive) return -1; // lose
    if (!gTowers[1].alive) return 1; // win
    return 0;
}

static void renderFrame(void) {
    COORD pos = {0, 0};
    SetConsoleCursorPosition(gConsole, pos);
    char buffer[MAP_H][MAP_W + 1];
    for (int y = 0; y < MAP_H; ++y) {
        memcpy(buffer[y], gMap[y], MAP_W + 1);
    }
    // place towers
    if (gTowers[0].alive) buffer[gTowers[0].y][gTowers[0].x] = 'T'; else buffer[gTowers[0].y][gTowers[0].x] = 't';
    if (gTowers[1].alive) buffer[gTowers[1].y][gTowers[1].x] = 'E'; else buffer[gTowers[1].y][gTowers[1].x] = 'e';
    // place minions
    for (int i = 0; i < MAX_MINIONS; ++i) {
        if (!gMinions[i].alive) continue;
        buffer[gMinions[i].y][gMinions[i].x] = 'm';
    }
    // hero
    if (gHero.alive) buffer[gHero.y][gHero.x] = 'H';

    for (int y = 0; y < MAP_H; ++y) {
        printf("%s\n", buffer[y]);
    }
    printf("Hero HP: %d/%d  ATK:%d  LV:%d XP:%d/%d  Kills:%d\n",
           gHero.hp, gHero.maxHp, gHero.attack, gHero.level, gHero.xp, gHero.nextLevelXp, gHero.kills);
    printf("Enemy Tower HP: %d/%d\n", gTowers[1].hp, gTowers[1].maxHp);
    printf("Player Tower HP: %d/%d\n", gTowers[0].hp, gTowers[0].maxHp);
    printf("Controls: WASD move, SPACE stop, J attack, Q quit\n");
}

static void *inputThread(void *arg) {
    (void)arg;
    while (1) {
        int ch = _getch();
        pthread_mutex_lock(&gStateMutex);
        if (!gRunning) {
            pthread_mutex_unlock(&gStateMutex);
            break;
        }
        switch (ch) {
            case 'w': case 'W': gHero.dirX = 0; gHero.dirY = -1; break;
            case 's': case 'S': gHero.dirX = 0; gHero.dirY = 1; break;
            case 'a': case 'A': gHero.dirX = -1; gHero.dirY = 0; break;
            case 'd': case 'D': gHero.dirX = 1; gHero.dirY = 0; break;
            case ' ': gHero.dirX = 0; gHero.dirY = 0; break;
            case 'j': case 'J': gHero.attackRequested = 1; break;
            case 'q': case 'Q': gRunning = 0; break;
            default: break;
        }
        pthread_mutex_unlock(&gStateMutex);
        if (ch == 'q' || ch == 'Q') break;
    }
    return NULL;
}

static void gameTick(void) {
    if (gTick % SPAWN_INTERVAL == 0) {
        spawnMinionWave();
    }
    updateHero();
    updateMinions();
    updateTowers();
    handleDeathsAndXP();
}

static void *gameLoop(void *arg) {
    (void)arg;
    while (1) {
        pthread_mutex_lock(&gStateMutex);
        if (!gRunning) {
            pthread_mutex_unlock(&gStateMutex);
            break;
        }
        gameTick();
        int result = checkVictory();
        if (result != 0) {
            gRunning = 0;
        }
        renderFrame();
        gTick++;
        int stillRunning = gRunning;
        pthread_mutex_unlock(&gStateMutex);
        Sleep(TICK_MS);
        if (!stillRunning) break;
    }
    return NULL;
}

int main(void) {
    srand((unsigned int)time(NULL));
    gConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    initMap();
    initHero();
    initTowers();
    initMinions();

    pthread_t inputTid, gameTid;
    pthread_create(&inputTid, NULL, inputThread, NULL);
    pthread_create(&gameTid, NULL, gameLoop, NULL);

    pthread_join(gameTid, NULL);
    pthread_mutex_lock(&gStateMutex);
    gRunning = 0;
    pthread_mutex_unlock(&gStateMutex);
    pthread_join(inputTid, NULL);

    pthread_mutex_lock(&gStateMutex);
    int result = checkVictory();
    pthread_mutex_unlock(&gStateMutex);
    if (result > 0) {
        printf("You Win!\n");
    } else if (result < 0) {
        printf("You Lose!\n");
    } else {
        printf("Game Over\n");
    }
    return 0;
}
