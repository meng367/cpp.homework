#ifndef USRAI_H
#define USRAI_H
#pragma once

#include "ai.h"
#include <unordered_map>

extern tagGame tagUsrGame;
extern ins UsrIns;
/*##########DO NOT MODIFY THE CODE ABOVE##########*/

class UsrAI:public AI
{
public:
    UsrAI(){this->id=0;}
    ~UsrAI(){}

private:
    void processData() override;
    int AddToIns(instruction ins) override
        {
            UsrIns.lock.lock();
            ins.id=UsrIns.g_id;
            UsrIns.g_id++;
            UsrIns.instructions.push(ins);
            UsrIns.lock.unlock();
            return ins.id;
        }
    tagInfo getInfo(){return tagUsrGame.getInfo();}
    void clearInsRet() override
    {
        tagUsrGame.clearInsRet();
    }
    /*##########DO NOT MODIFY THE CODE IN THE CLASS##########*/

    void scanMap(tagInfo& info);
    void getBasePos(tagInfo& info, int& bx, int& by);
    bool canPlace(tagInfo& info, int type, int x, int y);
    bool findPlace(tagInfo& info, int type, int& x, int& y);
    int findIdleBuilder(tagInfo& info);

    int countBuilding(tagInfo& info, int type);
    bool hasBuilding(tagInfo& info, int type);

    void buildOne(tagInfo& info, int type, int want,
                  int wood, int food, int stone, int gold);
    bool findPlaceNearGold(tagInfo& info, int type, int& x, int& y);
    void buildStockNearGold(tagInfo& info);
    void manageBuildings(tagInfo& info);

    void tryResearch(tagInfo& info, int buildingType, int action,
                     int wood, int food, int stone, int gold);
    void manageResearch(tagInfo& info);

    void cleanJob(tagInfo& info);
    int countJob(int job);
    int findResource(tagInfo& info, const tagFarmer& f, int type);
    int findFoodTarget(tagInfo& info, const tagFarmer& f);
    int findFarm(tagInfo& info, const tagFarmer& f);
    bool assignJob(tagInfo& info, const tagFarmer& f, int job);
    int chooseJob(tagInfo& info);
    void manageWorkers(tagInfo& info);

    bool canUpgradeBronze(tagInfo& info);
    void manageCenter(tagInfo& info);

    void trainArmy(tagInfo& info);
    bool findNearestUnit(tagInfo& info, const tagArmy& a,
                         int& targetSN, double& tx, double& ty,
                         double maxRange = 1e100);
    bool findNearestEnemyBuilding(tagInfo& info, const tagArmy& a,
                                  int& targetSN, double& tx, double& ty);
    int findEnemySiege(tagInfo& info);
    void findEnemyBase(tagInfo& info);
    bool nearEnemy(tagInfo& info, const tagArmy& a,
                   double& ex, double& ey, double& dist);
    bool findNearestTower(tagInfo& info, const tagArmy& a,
                          double& tx, double& ty);
    void getPriestExplorePoint(tagInfo& info, const tagArmy& a,
                               double& tx, double& ty);
    void manageTowers(tagInfo& info);
    void moveToRally(tagInfo& info, const tagArmy& a);
    void movePriestThrottled(tagInfo& info, tagArmy& a,
                             double tx, double ty);
    void managePriest(tagInfo& info, tagArmy& a);
    void manageArmy(tagInfo& info);
};

#endif // USRAI_H
