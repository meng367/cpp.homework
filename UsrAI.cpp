#include "UsrAI.h"
#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <climits>
#include <vector>
#include <map>
#include <set>
using namespace std;

tagGame tagUsrGame;
ins UsrIns;
/*##########DO NOT MODIFY THE CODE ABOVE##########*/

// 这块是跨帧记状态的东西，都写成全局变量，不然每帧都会被清掉。
// 0表示空地，1表示不能盖。之前想用vector，后来怕每次清地图太慢就用了数组。
static int g_grid[100][100];

// 村民在干什么活：1木头 2食物 3石头 4金子 5农田。
static map<int, int> g_job;

// 科技已经下了几次命令。有些科技是同一条链要研究两次的。
static map<int, int> g_techTimes;

// 专门盖房子的村民。课程设计试了很多次，发现指定一个人盖不容易乱。
static int g_builderSN = -1;

// 敌人基地大概在哪，后期进攻的时候用。
static int g_enemyX = -1;
static int g_enemyY = -1;

// 祭司探路的步数，隔几帧换一个位置。
static int g_priestStep = 0;
static int g_scoutStep = 0;

// 这一帧已经下过命令的对象。同一个对象连续下两条会被覆盖。
static set<int> g_usedSN;

// 有些调试信息不用每帧都打，隔一段时间打一次。
static int g_lastPrintFrame = -100000;

// 第三波结束、人口也差不多满了以后，就正式进入总攻，后面不能再退回来。
static bool g_attackStarted = false;

// 祭司移动命令防抖：避免同一帧内反复重置寻路。
static int g_priestMoveFrame = -100000;
static int g_priestLastDamageFrame = -100000;
static int g_priestLastBlood = -1;

static bool canUseSN(int sn)
{
    return sn >= 0 && g_usedSN.count(sn) == 0;
}

static void markSN(int sn)
{
    if (sn >= 0) {
        g_usedSN.insert(sn);
    }
}

static double toDetail(int block)
{
    return block * (double)BLOCKSIDELENGTH + (double)BLOCKSIDELENGTH / 2.0;
}

static int buildingSize(int type)
{
    if (type == BUILDING_HOME || type == BUILDING_ARROWTOWER || type == BUILDING_DOCK) {
        return 2;
    }
    return 3;
}

// 后面很多地方都要判断是不是已经进入进攻时间。
static bool isAttackTime(tagInfo& info)
{
    if (g_attackStarted) {
        return true;
    }
    if (info.GameFrame >= 24000 && info.Human_Num + 0.5 >= info.Human_MaxNum) {
        g_attackStarted = true;
        return true;
    }
    return false;
}

void UsrAI::scanMap(tagInfo& info)
{
    if (info.theMap == nullptr) {
        return;
    }

    for (int i = 0; i < MAP_L; ++i) {
        for (int j = 0; j < MAP_U; ++j) {
            const tagTerrain& t = (*info.theMap)[i][j];
            if (t.type == MAPPATTERN_OCEAN || t.type == MAPPATTERN_UNKNOWN) {
                g_grid[i][j] = 1;
            } else {
                g_grid[i][j] = 0;
            }
        }
    }

    for (const tagBuilding& b : info.buildings) {
        int s = buildingSize(b.Type);
        for (int x = b.BlockDR; x < b.BlockDR + s; ++x) {
            for (int y = b.BlockUR; y < b.BlockUR + s; ++y) {
                if (x >= 0 && x < MAP_L && y >= 0 && y < MAP_U) {
                    g_grid[x][y] = 1;
                }
            }
        }
    }

    for (const tagBuilding& b : info.enemy_buildings) {
        int s = buildingSize(b.Type);
        for (int x = b.BlockDR; x < b.BlockDR + s; ++x) {
            for (int y = b.BlockUR; y < b.BlockUR + s; ++y) {
                if (x >= 0 && x < MAP_L && y >= 0 && y < MAP_U) {
                    g_grid[x][y] = 1;
                }
            }
        }
    }

    // 资源旁边也先标掉，免得村民走一半卡住。
    for (const tagResource& r : info.resources) {
        if (r.BlockDR >= 0 && r.BlockDR < MAP_L && r.BlockUR >= 0 && r.BlockUR < MAP_U) {
            g_grid[r.BlockDR][r.BlockUR] = 1;
        }
    }
    for (const tagFarmer& f : info.farmers) {
        if (f.BlockDR >= 0 && f.BlockDR < MAP_L && f.BlockUR >= 0 && f.BlockUR < MAP_U) {
            g_grid[f.BlockDR][f.BlockUR] = 1;
        }
    }
    for (const tagArmy& a : info.armies) {
        if (a.BlockDR >= 0 && a.BlockDR < MAP_L && a.BlockUR >= 0 && a.BlockUR < MAP_U) {
            g_grid[a.BlockDR][a.BlockUR] = 1;
        }
    }
}

void UsrAI::getBasePos(tagInfo& info, int& bx, int& by)
{
    for (const tagBuilding& b : info.buildings) {
        if (b.Type == BUILDING_CENTER && b.Percent >= 100) {
            bx = b.BlockDR + buildingSize(b.Type) / 2;
            by = b.BlockUR + buildingSize(b.Type) / 2;
            return;
        }
    }

    for (const tagBuilding& b : info.buildings) {
        if (b.Percent >= 100) {
            bx = b.BlockDR + buildingSize(b.Type) / 2;
            by = b.BlockUR + buildingSize(b.Type) / 2;
            return;
        }
    }

    bx = MAP_L / 2;
    by = MAP_U / 2;
}

bool UsrAI::canPlace(tagInfo& info, int type, int x, int y)
{
    if (info.theMap == nullptr) {
        return false;
    }

    int s = buildingSize(type);
    if (x < 1 || y < 1 || x + s >= MAP_L - 1 || y + s >= MAP_U - 1) {
        return false;
    }

    int h = -1;
    for (int i = x; i < x + s; ++i) {
        for (int j = y; j < y + s; ++j) {
            if (g_grid[i][j] != 0) {
                return false;
            }

            const tagTerrain& t = (*info.theMap)[i][j];
            if (t.type != MAPPATTERN_GRASS && t.type != MAPPATTERN_DESERT) {
                return false;
            }

            if (h == -1) {
                h = t.height;
            } else if (t.height != h) {
                return false;
            }
        }
    }
    return true;
}

bool UsrAI::findPlace(tagInfo& info, int type, int& x, int& y)
{
    int bx = 0;
    int by = 0;
    getBasePos(info, bx, by);

    for (int r = 0; r < 36; ++r) {
        for (int dx = -r; dx <= r; ++dx) {
            for (int dy = -r; dy <= r; ++dy) {
                int nx = bx + dx;
                int ny = by + dy;
                if (canPlace(info, type, nx, ny)) {
                    x = nx;
                    y = ny;
                    return true;
                }
            }
        }
    }
    return false;
}

int UsrAI::findIdleBuilder(tagInfo& info)
{
    if (g_builderSN >= 0) {
        for (const tagFarmer& f : info.farmers) {
            if (f.SN == g_builderSN) {
                if (f.FarmerSort == FARMERTYPE_FARMER && f.NowState == HUMAN_STATE_IDLE) {
                    return f.SN;
                }
                break;
            }
        }
        g_builderSN = -1;
    }

    for (const tagFarmer& f : info.farmers) {
        if (f.FarmerSort == FARMERTYPE_FARMER && f.NowState == HUMAN_STATE_IDLE) {
            return f.SN;
        }
    }
    return -1;
}

int UsrAI::countBuilding(tagInfo& info, int type)
{
    int cnt = 0;
    for (const tagBuilding& b : info.buildings) {
        if (b.Type == type) {
            ++cnt;
        }
    }
    return cnt;
}

bool UsrAI::hasBuilding(tagInfo& info, int type)
{
    for (const tagBuilding& b : info.buildings) {
        if (b.Type == type && b.Percent >= 100) {
            return true;
        }
    }
    return false;
}

void UsrAI::buildOne(tagInfo& info, int type, int want,
                     int wood, int food, int stone, int gold)
{
    if (countBuilding(info, type) >= want) {
        return;
    }
    if (info.Wood < wood || info.Meat < food || info.Stone < stone || info.Gold < gold) {
        return;
    }

    int sn = findIdleBuilder(info);
    if (sn < 0) {
        return;
    }
    if (!canUseSN(sn)) {
        return;
    }

    int x = 0;
    int y = 0;
    if (!findPlace(info, type, x, y)) {
        return;
    }

    markSN(sn);
    g_builderSN = sn;
    g_job.erase(sn);
    HumanBuild(sn, type, x, y);
}

bool UsrAI::findPlaceNearGold(tagInfo& info, int type, int& x, int& y)
{
    int gx = -1;
    int gy = -1;
    double best = 1e100;

    for (const tagResource& r : info.resources) {
        if (r.Type != RESOURCE_GOLD) {
            continue;
        }

        int bx = 0;
        int by = 0;
        getBasePos(info, bx, by);
        double d = calDistance(r.BlockDR, r.BlockUR, bx, by);
        if (d < best) {
            best = d;
            gx = r.BlockDR;
            gy = r.BlockUR;
        }
    }

    if (gx < 0 || gy < 0) {
        return false;
    }

    for (int r = 0; r < 18; ++r) {
        for (int dx = -r; dx <= r; ++dx) {
            for (int dy = -r; dy <= r; ++dy) {
                int nx = gx + dx;
                int ny = gy + dy;
                if (canPlace(info, type, nx, ny)) {
                    x = nx;
                    y = ny;
                    return true;
                }
            }
        }
    }
    return false;
}

void UsrAI::buildStockNearGold(tagInfo& info)
{
    if (info.civilizationStage < CIVILIZATION_BRONZEAGE) {
        return;
    }

    // 已经有一个仓库离金矿很近，就不用再盖了。
    for (const tagResource& r : info.resources) {
        if (r.Type != RESOURCE_GOLD) {
            continue;
        }
        for (const tagBuilding& b : info.buildings) {
            if (b.Type != BUILDING_STOCK || b.Percent < 100) {
                continue;
            }
            int dx = abs(b.BlockDR - r.BlockDR);
            int dy = abs(b.BlockUR - r.BlockUR);
            if (dx + dy <= 10) {
                return;
            }
        }
    }

    if (info.Wood < BUILD_STOCK_WOOD) {
        return;
    }

    int sn = findIdleBuilder(info);
    if (sn < 0) {
        return;
    }
    if (!canUseSN(sn)) {
        return;
    }

    int x = 0;
    int y = 0;
    if (!findPlaceNearGold(info, BUILDING_STOCK, x, y)) {
        return;
    }

    markSN(sn);
    g_builderSN = sn;
    g_job.erase(sn);
    HumanBuild(sn, BUILDING_STOCK, x, y);
}

void UsrAI::manageBuildings(tagInfo& info)
{
    if (info.Human_Num >= info.Human_MaxNum - 1.5) {
        int wantHome = countBuilding(info, BUILDING_HOME) + 1;
        if (wantHome > 12) {
            wantHome = 12;
        }
        buildOne(info, BUILDING_HOME, wantHome, BUILD_HOUSE_WOOD, 0, 0, 0);
    }

    buildOne(info, BUILDING_MARKET, 1, BUILD_MARKET_WOOD, 0, 0, 0);
    buildOne(info, BUILDING_ARMYCAMP, 1, BUILD_ARMYCAMP_WOOD, 0, 0, 0);
    buildOne(info, BUILDING_RANGE, 2, BUILD_RANGE_WOOD, 0, 0, 0);
    buildOne(info, BUILDING_STABLE, 2, BUILD_STABLE_WOOD, 0, 0, 0);

    if (info.civilizationStage >= CIVILIZATION_BRONZEAGE) {
        buildOne(info, BUILDING_COLLAGE, 1, BUILD_COLLAGE_WOOD, 0, 0, 0);
        buildStockNearGold(info);
    }

    if (hasBuilding(info, BUILDING_MARKET)) {
        buildOne(info, BUILDING_FARM, 12, BUILD_FARM_WOOD, 0, 0, 0);
    }

    if (g_techTimes[BUILDING_GRANARY_ARROWTOWER] > 0) {
        buildOne(info, BUILDING_ARROWTOWER, 2, 0, 0, BUILD_ARROWTOWER_STONE, 0);
    }
}

void UsrAI::tryResearch(tagInfo& info, int buildingType, int action,
                        int wood, int food, int stone, int gold)
{
    int times = g_techTimes[action];
    int maxTimes = 1;

    if ((action == BUILDING_MARKET_WOOD_UPGRADE ||
         action == BUILDING_MARKET_FARM_UPGRADE ||
         action == BUILDING_STOCK_UPGRADE_USETOOL ||
         action == BUILDING_STOCK_UPGRADE_DEFENSE_INFANTRY ||
         action == BUILDING_STOCK_UPGRADE_DEFENSE_ARCHER ||
         action == BUILDING_STOCK_UPGRADE_DEFENSE_RIDER) &&
        info.civilizationStage >= CIVILIZATION_BRONZEAGE) {
        maxTimes = 2;
    }

    if (times >= maxTimes) {
        return;
    }
    if (info.Wood < wood || info.Meat < food || info.Stone < stone || info.Gold < gold) {
        return;
    }

    for (const tagBuilding& b : info.buildings) {
        if (b.Type != buildingType || b.Percent < 100 || b.Project != ACT_NULL) {
            continue;
        }
        if (!canUseSN(b.SN)) {
            continue;
        }

        markSN(b.SN);
        g_techTimes[action] = times + 1;
        BuildingAction(b.SN, action);
        return;
    }
}

void UsrAI::manageResearch(tagInfo& info)
{
    if (info.civilizationStage == CIVILIZATION_TOOLAGE) {
        bool readyUpgrade = canUpgradeBronze(info) && info.Meat >= 800;
        if (!readyUpgrade || info.Meat >= 850) {
            tryResearch(info, BUILDING_GRANARY, BUILDING_GRANARY_ARROWTOWER, 0, 50, 0, 0);
        }
        return;
    }

    if (info.civilizationStage < CIVILIZATION_BRONZEAGE) {
        return;
    }

    // 进入铜器后优先研发：车轮、伐木二级、铜器箭塔。
    tryResearch(info, BUILDING_MARKET, BUILDING_MARKET_WHEEL_UPGRADE, 100, 150, 0, 0);
    tryResearch(info, BUILDING_GRANARY, BUILDING_GRANARY_ARROWTOWE_UPGRADE, 0, 120, 50, 0);

    int woodTimes = g_techTimes[BUILDING_MARKET_WOOD_UPGRADE];
    if (woodTimes == 0) {
        tryResearch(info, BUILDING_MARKET, BUILDING_MARKET_WOOD_UPGRADE, 75, 120, 0, 0);
    } else if (woodTimes == 1) {
        tryResearch(info, BUILDING_MARKET, BUILDING_MARKET_WOOD_UPGRADE, 150, 170, 0, 0);
    }

    // 铜器兵种前置科技优先：复合弓、阔剑兵。
    tryResearch(info, BUILDING_RANGE, BUILDING_RANGE_UPGRADE_COMPOSITE_BOW, 100, 180, 0, 0);
    tryResearch(info, BUILDING_ARMYCAMP, BUILDING_ARMYCAMP_UPGRADE_BROADSWORD, 0, 140, 0, 50);

    int farmTechTimes = g_techTimes[BUILDING_MARKET_FARM_UPGRADE];
    if (farmTechTimes == 0 && info.Meat > 300) {
        tryResearch(info, BUILDING_MARKET, BUILDING_MARKET_FARM_UPGRADE, 50, 200, 0, 0);
    } else if (farmTechTimes == 1 && info.Meat > 350) {
        tryResearch(info, BUILDING_MARKET, BUILDING_MARKET_FARM_UPGRADE, 75, 250, 0, 0);
    }

    tryResearch(info, BUILDING_MARKET, BUILDING_MARKET_GOLD_UPGRADE, 100, 120, 0, 0);
    tryResearch(info, BUILDING_MARKET, BUILDING_MARKET_STONE_UPGRADE, 0, 100, 50, 0);

    int toolTimes = g_techTimes[BUILDING_STOCK_UPGRADE_USETOOL];
    if (toolTimes == 0) {
        tryResearch(info, BUILDING_STOCK, BUILDING_STOCK_UPGRADE_USETOOL, 0, 100, 0, 0);
    } else if (toolTimes == 1) {
        tryResearch(info, BUILDING_STOCK, BUILDING_STOCK_UPGRADE_USETOOL, 0, 200, 0, 120);
    }

    int infantryTimes = g_techTimes[BUILDING_STOCK_UPGRADE_DEFENSE_INFANTRY];
    if (infantryTimes == 0) {
        tryResearch(info, BUILDING_STOCK, BUILDING_STOCK_UPGRADE_DEFENSE_INFANTRY, 0, 75, 0, 0);
    } else if (infantryTimes == 1) {
        tryResearch(info, BUILDING_STOCK, BUILDING_STOCK_UPGRADE_DEFENSE_INFANTRY, 0, 100, 0, 50);
    }

    int archerTimes = g_techTimes[BUILDING_STOCK_UPGRADE_DEFENSE_ARCHER];
    if (archerTimes == 0) {
        tryResearch(info, BUILDING_STOCK, BUILDING_STOCK_UPGRADE_DEFENSE_ARCHER, 0, 100, 0, 0);
    } else if (archerTimes == 1) {
        tryResearch(info, BUILDING_STOCK, BUILDING_STOCK_UPGRADE_DEFENSE_ARCHER, 0, 125, 0, 50);
    }

    if ((int)info.armies.size() >= 8) {
        int riderTimes = g_techTimes[BUILDING_STOCK_UPGRADE_DEFENSE_RIDER];
        if (riderTimes == 0) {
            tryResearch(info, BUILDING_STOCK, BUILDING_STOCK_UPGRADE_DEFENSE_RIDER, 0, 125, 0, 0);
        } else if (riderTimes == 1) {
            tryResearch(info, BUILDING_STOCK, BUILDING_STOCK_UPGRADE_DEFENSE_RIDER, 0, 150, 0, 50);
        }
    }
}

void UsrAI::cleanJob(tagInfo& info)
{
    for (auto it = g_job.begin(); it != g_job.end(); ) {
        bool alive = false;
        for (const tagFarmer& f : info.farmers) {
            if (f.SN == it->first) {
                alive = true;
                break;
            }
        }
        if (alive) {
            ++it;
        } else {
            it = g_job.erase(it);
        }
    }
}

int UsrAI::countJob(int job)
{
    int cnt = 0;
    for (auto& p : g_job) {
        if (p.second == job) {
            ++cnt;
        }
    }
    return cnt;
}

int UsrAI::findResource(tagInfo& info, const tagFarmer& f, int type)
{
    int bestSN = -1;
    double bestDist = 1e100;

    for (const tagResource& r : info.resources) {
        if (r.Type != type) {
            continue;
        }
        double d = calDistance(f.DR, f.UR, r.DR, r.UR);
        if (d < bestDist) {
            bestDist = d;
            bestSN = r.SN;
        }
    }
    return bestSN;
}

int UsrAI::findFoodTarget(tagInfo& info, const tagFarmer& f)
{
    int sn = findResource(info, f, RESOURCE_BUSH);
    if (sn >= 0) {
        return sn;
    }

    sn = findResource(info, f, RESOURCE_GAZELLE);
    if (sn >= 0) {
        return sn;
    }

    return findFarm(info, f);
}

int UsrAI::findFarm(tagInfo& info, const tagFarmer& f)
{
    int bestSN = -1;
    double bestDist = 1e100;

    for (const tagBuilding& b : info.buildings) {
        if (b.Type != BUILDING_FARM || b.Percent < 100) {
            continue;
        }
        double bx = toDetail(b.BlockDR + buildingSize(b.Type) / 2);
        double by = toDetail(b.BlockUR + buildingSize(b.Type) / 2);
        double d = calDistance(f.DR, f.UR, bx, by);
        if (d < bestDist) {
            bestDist = d;
            bestSN = b.SN;
        }
    }
    return bestSN;
}

bool UsrAI::assignJob(tagInfo& info, const tagFarmer& f, int job)
{
    int targetSN = -1;

    if (job == 1) {
        targetSN = findResource(info, f, RESOURCE_TREE);
    } else if (job == 2) {
        targetSN = findFoodTarget(info, f);
    } else if (job == 3) {
        targetSN = findResource(info, f, RESOURCE_STONE);
    } else if (job == 4) {
        targetSN = findResource(info, f, RESOURCE_GOLD);
    } else if (job == 5) {
        targetSN = findFarm(info, f);
    }

    if (targetSN < 0) {
        return false;
    }
    if (!canUseSN(f.SN)) {
        return false;
    }

    markSN(f.SN);
    g_job[f.SN] = job;
    HumanAction(f.SN, targetSN);
    return true;
}

int UsrAI::chooseJob(tagInfo& info)
{
    int total = (int)info.farmers.size();
    int woodWant = max(2, total / 5);
    int foodWant = max(4, total / 2);
    int foodBusy = countJob(2) + countJob(5);

    int stoneWant = 0;
    if (countBuilding(info, BUILDING_ARROWTOWER) < 2) {
        stoneWant = 2;
    }

    int goldWant = 0;
    if (info.civilizationStage >= CIVILIZATION_BRONZEAGE) {
        goldWant = total >= 15 ? 3 : 2;
    }

    if (countJob(1) < woodWant) {
        return 1;
    }
    if (foodBusy < foodWant) {
        return 2;
    }
    if (stoneWant > 0 && countJob(3) < stoneWant) {
        return 3;
    }
    if (goldWant > 0 && countJob(4) < goldWant) {
        return 4;
    }
    if (countJob(5) < max(2, foodWant / 2)) {
        return 5;
    }
    return 2;
}

void UsrAI::manageWorkers(tagInfo& info)
{
    cleanJob(info);

    for (tagFarmer& f : info.farmers) {
        if (f.FarmerSort != FARMERTYPE_FARMER || f.NowState != HUMAN_STATE_IDLE) {
            continue;
        }

        g_job.erase(f.SN);

        int job = chooseJob(info);
        if (assignJob(info, f, job)) {
            continue;
        }

        int backup[5] = {1, 2, 3, 4, 5};
        bool ok = false;
        for (int i = 0; i < 5; ++i) {
            if (backup[i] == job) {
                continue;
            }
            if (assignJob(info, f, backup[i])) {
                ok = true;
                break;
            }
        }

        if (!ok) {
            int bx = 0;
            int by = 0;
            getBasePos(info, bx, by);
            if (canUseSN(f.SN)) {
                markSN(f.SN);
                HumanMove(f.SN, toDetail(bx), toDetail(by));
            }
        }
    }
}

bool UsrAI::canUpgradeBronze(tagInfo& info)
{
    return info.civilizationStage == CIVILIZATION_TOOLAGE &&
           hasBuilding(info, BUILDING_MARKET) &&
           (hasBuilding(info, BUILDING_STABLE) || hasBuilding(info, BUILDING_RANGE));
}

void UsrAI::manageCenter(tagInfo& info)
{
    int targetFarmer = info.civilizationStage < CIVILIZATION_BRONZEAGE ? 22 : 0;

    for (const tagBuilding& b : info.buildings) {
        if (b.Type != BUILDING_CENTER || b.Percent < 100 || b.Project != ACT_NULL) {
            continue;
        }

        if (canUpgradeBronze(info) && info.Meat >= 800) {
            if (!canUseSN(b.SN)) {
                continue;
            }
            markSN(b.SN);
            BuildingAction(b.SN, BUILDING_CENTER_UPGRADE);
            return;
        }

        if (info.civilizationStage < CIVILIZATION_BRONZEAGE &&
            info.Human_Num < targetFarmer &&
            info.Human_Num + 0.01 < info.Human_MaxNum &&
            info.Meat >= 50) {
            if (!canUseSN(b.SN)) {
                continue;
            }
            markSN(b.SN);
            BuildingAction(b.SN, BUILDING_CENTER_CREATEFARMER);
        }
    }
}

void UsrAI::trainArmy(tagInfo& info)
{
    if (info.civilizationStage < CIVILIZATION_BRONZEAGE) {
        if (canUpgradeBronze(info) && info.Meat >= 800) {
            return;
        }
        if (info.Meat > 950) {
            for (const tagBuilding& b : info.buildings) {
                if (b.Percent < 100 || b.Project != ACT_NULL) {
                    continue;
                }
                if (info.Human_Num + 0.01 >= info.Human_MaxNum) {
                    return;
                }

                int action = 0;
                if (b.Type == BUILDING_ARMYCAMP &&
                    info.Meat >= BUILDING_ARMYCAMP_CREATE_SLINGER_FOOD &&
                    info.Stone >= BUILDING_ARMYCAMP_CREATE_SLINGER_STONE) {
                    action = BUILDING_ARMYCAMP_CREATE_SLINGER;
                } else if (b.Type == BUILDING_RANGE &&
                           info.Meat >= BUILDING_RANGE_CREATE_BOWMAN_FOOD &&
                           info.Wood >= BUILDING_RANGE_CREATE_BOWMAN_WOOD) {
                    action = BUILDING_RANGE_CREATE_BOWMAN;
                } else if (b.Type == BUILDING_STABLE &&
                           info.Meat >= BUILDING_STABLE_CREATE_SCOUT_FOOD) {
                    action = BUILDING_STABLE_CREATE_SCOUT;
                }

                if (action != 0 && canUseSN(b.SN)) {
                    markSN(b.SN);
                    BuildingAction(b.SN, action);
                }
            }
        }
        return;
    }

    int scoutCount = 0;
    for (const tagArmy& u : info.armies) {
        if (u.Sort == AT_SCOUT) {
            ++scoutCount;
        }
    }

    for (const tagBuilding& b : info.buildings) {
        if (b.Percent < 100 || b.Project != ACT_NULL) {
            continue;
        }
        if (info.Human_Num + 0.01 >= info.Human_MaxNum) {
            return;
        }

        int action = 0;
        if (b.Type == BUILDING_ARMYCAMP) {
            if (g_techTimes[BUILDING_ARMYCAMP_UPGRADE_BROADSWORD] > 0 &&
                info.Meat >= 35 && info.Gold >= 15) {
                action = BUILDING_ARMYCAMP_CREATE_BROADSWORD;
            } else if (info.Meat >= 50) {
                action = BUILDING_ARMYCAMP_CREATE_CLUBMAN;
            }
        } else if (b.Type == BUILDING_RANGE) {
            if (g_techTimes[BUILDING_MARKET_WHEEL_UPGRADE] > 0 &&
                info.Meat >= 40 && info.Wood >= 70) {
                action = BUILDING_RANGE_CREATE_CHARIOT_ARCHER;
            } else if (g_techTimes[BUILDING_RANGE_UPGRADE_COMPOSITE_BOW] > 0 &&
                info.Meat >= 40 && info.Gold >= 20) {
                action = BUILDING_RANGE_CREATE_COMPOSITE_BOWMAN;
            } else if (info.Meat >= 40 && info.Wood >= 20) {
                action = BUILDING_RANGE_CREATE_BOWMAN;
            }
        } else if (b.Type == BUILDING_STABLE) {
            if (g_techTimes[BUILDING_MARKET_WHEEL_UPGRADE] > 0 &&
                info.Meat >= 40 && info.Wood >= 60) {
                action = BUILDING_STABLE_CREATE_CHARIOT;
            } else if (scoutCount < 2 && info.Meat >= 100) {
                action = BUILDING_STABLE_CREATE_SCOUT;
                ++scoutCount;
            } else if (info.Meat >= 70 && info.Gold >= 80) {
                action = BUILDING_STABLE_CREATE_CAVALRY;
            } else if (info.Meat >= 100) {
                action = BUILDING_STABLE_CREATE_SCOUT;
            }
        } else if (b.Type == BUILDING_COLLAGE) {
            if (info.Meat >= 60 && info.Gold >= 40) {
                action = BUILDING_COLLAGE_CREATE_HOPLITE;
            }
        }

        if (action != 0 && canUseSN(b.SN)) {
            markSN(b.SN);
            BuildingAction(b.SN, action);
        }
    }
}

bool UsrAI::findNearestUnit(tagInfo& info, const tagArmy& a,
                            int& targetSN, double& tx, double& ty,
                            double maxRange)
{
    double best = 1e100;
    targetSN = -1;

    for (const tagArmy& e : info.enemy_armies) {
        double d = calDistance(a.DR, a.UR, e.DR, e.UR);
        if (d < best && d <= maxRange) {
            best = d;
            targetSN = e.SN;
            tx = e.DR;
            ty = e.UR;
        }
    }
    for (const tagFarmer& e : info.enemy_farmers) {
        double d = calDistance(a.DR, a.UR, e.DR, e.UR);
        if (d < best && d <= maxRange) {
            best = d;
            targetSN = e.SN;
            tx = e.DR;
            ty = e.UR;
        }
    }
    return targetSN >= 0;
}

bool UsrAI::findNearestEnemyBuilding(tagInfo& info, const tagArmy& a,
                                     int& targetSN, double& tx, double& ty)
{
    double best = 1e100;
    targetSN = -1;

    for (const tagBuilding& e : info.enemy_buildings) {
        if (e.Type == BUILDING_SIEGE) {
            continue;
        }

        double bx = toDetail(e.BlockDR + buildingSize(e.Type) / 2);
        double by = toDetail(e.BlockUR + buildingSize(e.Type) / 2);
        double d = calDistance(a.DR, a.UR, bx, by);
        if (d < best) {
            best = d;
            targetSN = e.SN;
            tx = bx;
            ty = by;
        }
    }
    return targetSN >= 0;
}

int UsrAI::findEnemySiege(tagInfo& info)
{
    for (const tagBuilding& b : info.enemy_buildings) {
        if (b.Type == BUILDING_SIEGE) {
            return b.SN;
        }
    }
    return -1;
}

void UsrAI::findEnemyBase(tagInfo& info)
{
    if (info.enemy_buildings.empty()) {
        return;
    }

    for (const tagBuilding& b : info.enemy_buildings) {
        if (b.Type == BUILDING_ARROWTOWER) {
            g_enemyX = b.BlockDR + buildingSize(b.Type) / 2;
            g_enemyY = b.BlockUR + buildingSize(b.Type) / 2;
            return;
        }
    }

    int sx = 0;
    int sy = 0;
    int n = 0;
    for (const tagBuilding& b : info.enemy_buildings) {
        sx += b.BlockDR + buildingSize(b.Type) / 2;
        sy += b.BlockUR + buildingSize(b.Type) / 2;
        ++n;
    }
    g_enemyX = sx / n;
    g_enemyY = sy / n;
}

bool UsrAI::nearEnemy(tagInfo& info, const tagArmy& a,
                      double& ex, double& ey, double& dist)
{
    dist = 1e100;
    bool found = false;

    for (const tagArmy& e : info.enemy_armies) {
        double d = calDistance(a.DR, a.UR, e.DR, e.UR);
        if (d < dist) {
            dist = d;
            ex = e.DR;
            ey = e.UR;
            found = true;
        }
    }
    for (const tagFarmer& e : info.enemy_farmers) {
        double d = calDistance(a.DR, a.UR, e.DR, e.UR);
        if (d < dist) {
            dist = d;
            ex = e.DR;
            ey = e.UR;
            found = true;
        }
    }
    for (const tagBuilding& e : info.enemy_buildings) {
        if (e.Type == BUILDING_SIEGE) {
            continue;
        }
        double bx = toDetail(e.BlockDR + buildingSize(e.Type) / 2);
        double by = toDetail(e.BlockUR + buildingSize(e.Type) / 2);
        double d = calDistance(a.DR, a.UR, bx, by);
        if (d < dist) {
            dist = d;
            ex = bx;
            ey = by;
            found = true;
        }
    }

    return found;
}

bool UsrAI::findNearestTower(tagInfo& info, const tagArmy& a,
                             double& tx, double& ty)
{
    double best = 1e100;
    bool found = false;

    for (const tagBuilding& b : info.buildings) {
        if (b.Type != BUILDING_ARROWTOWER || b.Percent < 100) {
            continue;
        }

        double bx = toDetail(b.BlockDR + buildingSize(b.Type) / 2);
        double by = toDetail(b.BlockUR + buildingSize(b.Type) / 2);
        double towerDist = calDistance(a.DR, a.UR, bx, by);
        if (towerDist < best) {
            best = towerDist;

            double off = 1.5 * (double)BLOCKSIDELENGTH;
            double cand[4][2] = {
                {bx + off, by},
                {bx - off, by},
                {bx, by + off},
                {bx, by - off}
            };

            int pick = 0;
            double pickDist = 1e100;
            for (int i = 0; i < 4; ++i) {
                double d = calDistance(a.DR, a.UR, cand[i][0], cand[i][1]);
                if (d < pickDist) {
                    pickDist = d;
                    pick = i;
                }
            }

            tx = cand[pick][0];
            ty = cand[pick][1];
            found = true;
        }
    }

    if (found) {
        double B = (double)BLOCKSIDELENGTH;
        double minD = 1.5 * B;
        double maxX = (MAP_L - 1.5) * B;
        double maxY = (MAP_U - 1.5) * B;
        if (tx < minD) {
            tx = minD;
        }
        if (tx > maxX) {
            tx = maxX;
        }
        if (ty < minD) {
            ty = minD;
        }
        if (ty > maxY) {
            ty = maxY;
        }
    }

    return found;
}

void UsrAI::getPriestExplorePoint(tagInfo& info, const tagArmy& a,
                                  double& tx, double& ty)
{
    int bx = 0;
    int by = 0;
    getBasePos(info, bx, by);

    int dirs[8][2] = {
        {1, 0}, {1, 1}, {0, 1}, {-1, 1},
        {-1, 0}, {-1, -1}, {0, -1}, {1, -1}
    };
    int dir = g_priestStep % 8;
    int layer = g_priestStep / 8;
    if (layer > 8) {
        layer = 8;
    }
    int radius = 6 + layer * 9;

    int x = bx + dirs[dir][0] * radius;
    int y = by + dirs[dir][1] * radius;

    if (x < 2) {
        x = 2;
    }
    if (x >= MAP_L - 2) {
        x = MAP_L - 3;
    }
    if (y < 2) {
        y = 2;
    }
    if (y >= MAP_U - 2) {
        y = MAP_U - 3;
    }

    tx = toDetail(x);
    ty = toDetail(y);
}

void UsrAI::manageTowers(tagInfo& info)
{
    for (const tagBuilding& b : info.buildings) {
        if (b.Type != BUILDING_ARROWTOWER || b.Percent < 100) {
            continue;
        }

        if (b.Project > 0) {
            continue;
        }
        if (!canUseSN(b.SN)) {
            continue;
        }

        double bx = toDetail(b.BlockDR + buildingSize(b.Type) / 2);
        double by = toDetail(b.BlockUR + buildingSize(b.Type) / 2);

        int targetSN = -1;
        double best = 1e100;
        double range = 8.0 * (double)BLOCKSIDELENGTH;

        for (const tagArmy& e : info.enemy_armies) {
            double d = calDistance(bx, by, e.DR, e.UR);
            if (d < best) {
                best = d;
                targetSN = e.SN;
            }
        }
        for (const tagFarmer& e : info.enemy_farmers) {
            double d = calDistance(bx, by, e.DR, e.UR);
            if (d < best) {
                best = d;
                targetSN = e.SN;
            }
        }

        if (!(targetSN >= 0 && best <= range)) {
            best = 1e100;
            targetSN = -1;
            for (const tagBuilding& e : info.enemy_buildings) {
                if (e.Type == BUILDING_SIEGE) {
                    continue;
                }
                double ex = toDetail(e.BlockDR + buildingSize(e.Type) / 2);
                double ey = toDetail(e.BlockUR + buildingSize(e.Type) / 2);
                double d = calDistance(bx, by, ex, ey);
                if (d < best) {
                    best = d;
                    targetSN = e.SN;
                }
            }
        }

        if (targetSN >= 0 && best <= range) {
            markSN(b.SN);
            HumanAction(b.SN, targetSN);
        }
    }
}

void UsrAI::moveToRally(tagInfo& info, const tagArmy& a)
{
    int bx = 0;
    int by = 0;
    getBasePos(info, bx, by);

    if (!isAttackTime(info)) {
        if (a.Sort == AT_SCOUT) {
            int dir = g_scoutStep % 8;
            int layer = g_scoutStep / 8;
            if (layer > 10) {
                layer = 10;
            }
            int radius = 8 + layer * 8;
            int dirs[8][2] = {
                {1, 0}, {1, 1}, {0, 1}, {-1, 1},
                {-1, 0}, {-1, -1}, {0, -1}, {1, -1}
            };
            int x = bx + dirs[dir][0] * radius;
            int y = by + dirs[dir][1] * radius;
            if (x < 2) {
                x = 2;
            }
            if (x >= MAP_L - 2) {
                x = MAP_L - 3;
            }
            if (y < 2) {
                y = 2;
            }
            if (y >= MAP_U - 2) {
                y = MAP_U - 3;
            }
            HumanMove(a.SN, toDetail(x), toDetail(y));
            ++g_scoutStep;
            return;
        }

        int ox = (a.SN / 3) % 7 - 3;
        int oy = (a.SN / 5) % 7 - 3;
        int tx = bx + 2 + ox;
        int ty = by + 2 + oy;
        if (tx < 1) {
            tx = 1;
        }
        if (ty < 1) {
            ty = 1;
        }
        if (tx >= MAP_L - 1) {
            tx = MAP_L - 2;
        }
        if (ty >= MAP_U - 1) {
            ty = MAP_U - 2;
        }
        HumanMove(a.SN, toDetail(tx), toDetail(ty));
        return;
    }

    if (a.Sort == AT_SCOUT) {
        if (g_enemyX >= 0) {
            double hx = toDetail(bx);
            double hy = toDetail(by);
            double ex = toDetail(g_enemyX);
            double ey = toDetail(g_enemyY);
            HumanMove(a.SN, hx + (ex - hx) * 0.62, hy + (ey - hy) * 0.62);
        } else {
            int ex = MAP_L - 1 - bx;
            int ey = MAP_U - 1 - by;
            HumanMove(a.SN, toDetail(ex), toDetail(ey));
        }
        return;
    }

    if (g_enemyX >= 0) {
        double hx = toDetail(bx);
        double hy = toDetail(by);
        double ex = toDetail(g_enemyX);
        double ey = toDetail(g_enemyY);
        HumanMove(a.SN, hx + (ex - hx) * 0.72, hy + (ey - hy) * 0.72);
        return;
    }

    int ex = MAP_L - 1 - bx;
    int ey = MAP_U - 1 - by;
    if (ex < 10) {
        ex = 50;
    }
    if (ey < 10) {
        ey = 50;
    }
    HumanMove(a.SN, toDetail(ex), toDetail(ey));
}

void UsrAI::movePriestThrottled(tagInfo& info, tagArmy& a,
                                double tx, double ty)
{
    if (!canUseSN(a.SN)) {
        return;
    }

    double B = (double)BLOCKSIDELENGTH;
    if (calDistance(a.DR, a.UR, tx, ty) <= 1.0 * B) {
        return;
    }
    if (info.GameFrame - g_priestMoveFrame < 20) {
        return;
    }

    markSN(a.SN);
    HumanMove(a.SN, tx, ty);
    g_priestMoveFrame = info.GameFrame;
}

void UsrAI::managePriest(tagInfo& info, tagArmy& a)
{
    int siegeSN = findEnemySiege(info);
    double ex = 0.0;
    double ey = 0.0;
    double enemyDist = 0.0;
    bool danger = nearEnemy(info, a, ex, ey, enemyDist);
    if (siegeSN >= 0 && (!danger || enemyDist >= 9.0 * (double)BLOCKSIDELENGTH)) {
        if (canUseSN(a.SN)) {
            markSN(a.SN);
            HumanAction(a.SN, siegeSN);
        }
        return;
    }

    double towerX = 0.0;
    double towerY = 0.0;
    bool haveTower = findNearestTower(info, a, towerX, towerY);

    bool recentlyHurt = info.GameFrame - g_priestLastDamageFrame < 60;
    if (recentlyHurt) {
        double B = (double)BLOCKSIDELENGTH;
        int bx = 0;
        int by = 0;
        getBasePos(info, bx, by);

        double tx = toDetail(bx + 2);
        double ty = toDetail(by + 2);

        if (!isAttackTime(info) && haveTower) {
            tx = towerX;
            ty = towerY;
        } else if (danger) {
            double dx = a.DR - ex;
            double dy = a.UR - ey;
            double len = sqrt(dx * dx + dy * dy);
            if (len < 0.01) {
                dx = a.DR - toDetail(bx);
                dy = a.UR - toDetail(by);
                len = sqrt(dx * dx + dy * dy);
            }
            if (len < 0.01) {
                dx = 1.0;
                dy = 0.0;
                len = 1.0;
            }

            tx = a.DR + dx / len * 8.0 * B;
            ty = a.UR + dy / len * 8.0 * B;
            tx = tx * 0.55 + toDetail(bx) * 0.45;
            ty = ty * 0.55 + toDetail(by) * 0.45;
        }

        double minD = 2.0 * B;
        double maxX = (MAP_L - 3.0) * B;
        double maxY = (MAP_U - 3.0) * B;
        if (tx < minD) {
            tx = minD;
        }
        if (tx > maxX) {
            tx = maxX;
        }
        if (ty < minD) {
            ty = minD;
        }
        if (ty > maxY) {
            ty = maxY;
        }

        movePriestThrottled(info, a, tx, ty);
        return;
    }

    bool enemyNear = danger && enemyDist < 10.0 * (double)BLOCKSIDELENGTH;
    if (enemyNear && !isAttackTime(info)) {
        if (!haveTower) {
            int bx = 0;
            int by = 0;
            getBasePos(info, bx, by);
            towerX = toDetail(bx + 2);
            towerY = toDetail(by + 2);
        }

        movePriestThrottled(info, a, towerX, towerY);
        return;
    }

    if (danger) {
        double B = (double)BLOCKSIDELENGTH;
        if (enemyDist < 9.0 * B) {
            double dx = a.DR - ex;
            double dy = a.UR - ey;
            double len = sqrt(dx * dx + dy * dy);
            if (len < 0.01) {
                int bx = 0;
                int by = 0;
                getBasePos(info, bx, by);
                dx = a.DR - toDetail(bx);
                dy = a.UR - toDetail(by);
                len = sqrt(dx * dx + dy * dy);
            }
            if (len < 0.01) {
                dx = 1.0;
                dy = 0.0;
                len = 1.0;
            }

            int bx = 0;
            int by = 0;
            getBasePos(info, bx, by);
            double tx = a.DR + dx / len * 8.0 * B;
            double ty = a.UR + dy / len * 8.0 * B;
            tx = tx * 0.65 + toDetail(bx) * 0.35;
            ty = ty * 0.65 + toDetail(by) * 0.35;

            double minD = 2.0 * B;
            double maxX = (MAP_L - 3.0) * B;
            double maxY = (MAP_U - 3.0) * B;
            if (tx < minD) {
                tx = minD;
            }
            if (tx > maxX) {
                tx = maxX;
            }
            if (ty < minD) {
                ty = minD;
            }
            if (ty > maxY) {
                ty = maxY;
            }

            movePriestThrottled(info, a, tx, ty);
            return;
        }
    }

    if (isAttackTime(info)) {
        if (canUseSN(a.SN)) {
            int bx = 0;
            int by = 0;
            getBasePos(info, bx, by);

            double tx = toDetail(bx + 3);
            double ty = toDetail(by + 3);
            if (haveTower) {
                tx = towerX;
                ty = towerY;
            }

            double B = (double)BLOCKSIDELENGTH;
            double minD = 2.0 * B;
            double maxX = (MAP_L - 3.0) * B;
            double maxY = (MAP_U - 3.0) * B;
            if (tx < minD) {
                tx = minD;
            }
            if (tx > maxX) {
                tx = maxX;
            }
            if (ty < minD) {
                ty = minD;
            }
            if (ty > maxY) {
                ty = maxY;
            }

            movePriestThrottled(info, a, tx, ty);
        }
        return;
    }

    if (canUseSN(a.SN)) {
        double tx = 0.0;
        double ty = 0.0;
        getPriestExplorePoint(info, a, tx, ty);
        if (calDistance(a.DR, a.UR, tx, ty) <= 1.0 * (double)BLOCKSIDELENGTH) {
            ++g_priestStep;
        } else {
            int before = g_priestMoveFrame;
            movePriestThrottled(info, a, tx, ty);
            if (g_priestMoveFrame != before) {
                ++g_priestStep;
            }
        }
    }
}

void UsrAI::manageArmy(tagInfo& info)
{
    findEnemyBase(info);

    int focusSN = -1;
    double focusTX = 0.0;
    double focusTY = 0.0;
    bool haveFocus = false;
    if (isAttackTime(info)) {
        for (const tagArmy& u : info.armies) {
            if (u.Sort == AT_PRIEST || u.Sort == AT_SCOUT) {
                continue;
            }
            if (findNearestUnit(info, u, focusSN, focusTX, focusTY)) {
                haveFocus = true;
                break;
            }
        }
    }

    for (tagArmy& a : info.armies) {
        if (!canUseSN(a.SN)) {
            continue;
        }

        if (a.Sort == AT_PRIEST) {
            if (g_priestLastBlood >= 0 && a.Blood < g_priestLastBlood) {
                g_priestLastDamageFrame = info.GameFrame;
            }
            g_priestLastBlood = a.Blood;

            bool recentlyHurt = info.GameFrame - g_priestLastDamageFrame < 60;
            double ex = 0.0;
            double ey = 0.0;
            double enemyDist = 0.0;
            bool danger = nearEnemy(info, a, ex, ey, enemyDist);
            bool enemyNear = danger && enemyDist < 10.0 * (double)BLOCKSIDELENGTH;
            if (a.NowState == HUMAN_STATE_IDLE || recentlyHurt || enemyNear) {
                managePriest(info, a);
            }
            continue;
        }

        if (a.NowState != HUMAN_STATE_IDLE) {
            continue;
        }

        if (a.Sort == AT_SCOUT) {
            markSN(a.SN);
            moveToRally(info, a);
            continue;
        }

        int targetSN = -1;
        double tx = 0.0;
        double ty = 0.0;

        if (isAttackTime(info)) {
            if (haveFocus) {
                targetSN = focusSN;
                tx = focusTX;
                ty = focusTY;
            } else if (findNearestEnemyBuilding(info, a, targetSN, tx, ty)) {
                // 没有可见敌军单位时攻击敌方建筑。
            } else {
                markSN(a.SN);
                moveToRally(info, a);
                continue;
            }

            markSN(a.SN);
            if (a.Sort == AT_STONE_THROWER) {
                PinPointStrike(a.SN, tx, ty);
            } else {
                HumanAction(a.SN, targetSN);
            }
            continue;
        }

        double engageRange = 12.0 * (double)BLOCKSIDELENGTH;
        if (findNearestUnit(info, a, targetSN, tx, ty, engageRange)) {
            markSN(a.SN);
            if (a.Sort == AT_STONE_THROWER) {
                PinPointStrike(a.SN, tx, ty);
            } else {
                HumanAction(a.SN, targetSN);
            }
            continue;
        }

        markSN(a.SN);
        moveToRally(info, a);
    }
}

void UsrAI::processData()
{
    tagInfo info = getInfo();

    static int g_lastFrame = 0;
    if (info.GameFrame < g_lastFrame) {
        g_priestMoveFrame = -100000;
        g_priestLastDamageFrame = -100000;
        g_priestLastBlood = -1;
        g_priestStep = 0;
        g_scoutStep = 0;
    }
    g_lastFrame = info.GameFrame;

    g_usedSN.clear();
    scanMap(info);
    manageCenter(info);
    manageBuildings(info);
    manageResearch(info);
    manageWorkers(info);
    trainArmy(info);
    manageTowers(info);
    manageArmy(info);

    if (info.GameFrame - g_lastPrintFrame >= 500) {
        g_lastPrintFrame = info.GameFrame;
        DebugText(QString("frame=%1 stage=%2 pop=%3 wood=%4 meat=%5 stone=%6 gold=%7")
                  .arg(info.GameFrame)
                  .arg(info.civilizationStage)
                  .arg(info.Human_Num)
                  .arg(info.Wood)
                  .arg(info.Meat)
                  .arg(info.Stone)
                  .arg(info.Gold));
    }
}
