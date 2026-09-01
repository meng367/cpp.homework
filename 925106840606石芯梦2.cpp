#include "UsrAI.h"
#include<set>
#include <iostream>
#include<unordered_map>
#include<list>
#include <cstdlib>
#include<cmath>
using namespace std;
tagGame tagUsrGame;
ins UsrIns;
/*##########DO NOT MODIFY THE CODE ABOVE##########*/
int gameMap[100][100];
const int UNKNOWN_SEA = -1;
const int LAND_OK = 0;
const int RES_FLAG = 10;
const int BUILD_BASE = 20;
void buildMap(tagInfo &info)
{
    const int UNIT_BASE = 30;
    for(int i = 0; i < 100; i++)
    {
        for(int j = 0; j < 100; j++)
        {
            gameMap[i][j] = UNKNOWN_SEA;
        }
    }

    auto& twoMap = *(info.theMap);
    for(int i = 0; i < twoMap.size(); i++)
    {
        auto& row = twoMap[i];
        for(int j = 0; j < row.size(); j++)
        {
            auto& terrain = row[j];
            int bx = i;
            int by = j;
            if (bx >=0 && bx < 100 && by >=0 && by <100)
            {
                gameMap[bx][by] = LAND_OK;
            }
        }
    }

    for(auto& res : info.resources)
    {
        int bx = res.BlockDR;
        int by = res.BlockUR;
        if (bx >=0 && bx < 100 && by >=0 && by <100)
        {
            gameMap[bx][by]=RES_FLAG+res.SN;
        }
    }

    for(auto& build : info.buildings)
    {
        int bx = build.BlockDR;
        int by = build.BlockUR;
        if(bx >= 0 && bx < 100 && by >= 0 && by < 100)
        {
            gameMap[bx][by] = BUILD_BASE + build.SN;
        }
    }
    for(auto& human : info.farmers)
    {
        int bx = human.BlockDR;
        int by = human.BlockUR;
        if(bx >= 0 && bx < 100 && by >=0 && by < 100)
        {
            gameMap[bx][by] = UNIT_BASE + human.SN;
        }
    }

    for(auto& soldier : info.armies)
    {
        int bx = soldier.BlockDR;
        int by = soldier.BlockUR;
        if(bx >= 0 && bx < 100 && by >=0 && by < 100)
        {
            gameMap[bx][by] = UNIT_BASE + soldier.SN;
        }
    }

    for(auto& build : info.enemy_buildings)
    {
        int bx = build.BlockDR;
        int by = build.BlockUR;
        if(bx >= 0 && bx < 100 && by >= 0 && by < 100)
        {
            gameMap[bx][by] = BUILD_BASE + build.SN;
        }
    }

    for(auto& human : info.enemy_farmers)
    {
        int bx = human.BlockDR;
        int by = human.BlockUR;
        if(bx >= 0 && bx < 100 && by >=0 && by < 100)
        {
            gameMap[bx][by] = UNIT_BASE + human.SN;
        }
    }
    for(auto& soldier : info.enemy_armies)
    {
        int bx = soldier.BlockDR;
        int by = soldier.BlockUR;
        if(bx >= 0 && bx < 100 && by >=0 && by < 100)
        {
            gameMap[bx][by] = UNIT_BASE + soldier.SN;
        }
    }
}
void UsrAI::processData()
{
    tagInfo info = tagUsrGame.getInfo();
        buildMap(info);
        farmerJob.clear();
        unordered_map<int, bool>priestTaskThisFrame;
        for(auto& farmer : info.farmers)
        {
            if(farmer.NowState != HUMAN_STATE_IDLE)
            {
                farmerJob.remove(farmer.SN);
                continue;
            }
            if(farmerJob.contains(farmer.SN))
                continue;

            int bestResSN = -1;
            double minDis = 1e9;
            for(auto& res : info.resources)
            {
                double dx = farmer.DR - res.DR;
                double dy = farmer.UR - res.UR;
                double dist = sqrt(dx*dx + dy*dy);
                if(dist < minDis)
                {
                    minDis = dist;
                    bestResSN = res.SN;
                }
            }
            if(bestResSN != -1)
            {
                HumanAction(farmer.SN,bestResSN);
                farmerJob.insert(farmer.SN,bestResSN);
            }
        }

          const double DETECT_RANGE = 200.0;
          const double MAP_MAX = 3000.0;
          for (auto &unit : info.armies)
          {
              if (unit.Sort != AT_PRIEST)
              {
                  continue;
              }
              if (priestTaskThisFrame.count(unit.SN))
              {
                  continue;
              }
              if (unit.NowState != HUMAN_STATE_IDLE)
              {
                  continue;
              }
              bool danger = false;
              for (auto &enemy : info.enemy_farmers)
              {
                  double dx = unit.DR - enemy.DR;
                  double dy = unit.UR - enemy.UR;
                  double dist = sqrt(dx * dx + dy * dy);
                  if (dist < DETECT_RANGE)
                  {
                      danger = true;
                      break;
                  }
              }
              if (!danger)
              {
                  for (auto &enemy : info.enemy_armies)
                  {
                      double dx = unit.DR - enemy.DR;
                      double dy = unit.UR - enemy.UR;
                      double dist = sqrt(dx * dx + dy * dy);
                      if (dist < DETECT_RANGE)
                      {
                          danger = true;
                          break;
                      }
                  }
              }

                if (danger)
                {
                    for (auto &build : info.buildings)
                    {
                        double fineDR = build.BlockDR + 0.5;
                        double fineUR = build.BlockUR + 0.5;
                        HumanMove(unit.SN, fineDR, fineUR);
                        priestTaskThisFrame[unit.SN] = true;
                        break;
                    }
                }
                else
                {
                    double targetDR = rand() / (double)RAND_MAX * MAP_MAX;
                    double targetUR = rand() / (double)RAND_MAX * MAP_MAX;
                    HumanMove(unit.SN, targetDR, targetUR);
                    priestTaskThisFrame[unit.SN] = true;
                }
            }
        }
