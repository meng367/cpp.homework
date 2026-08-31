#include "UsrAI.h"
#include<set>
#include <iostream>
#include<unordered_map>
#include<list>
#include <cstdlib>

using namespace std;
tagGame tagUsrGame;
ins UsrIns;
/*##########DO NOT MODIFY THE CODE ABOVE##########*/

void UsrAI::processData()
{
    tagInfo info = tagUsrGame.getInfo();

        farmerJob.clear();
        aiStage = 0;
        buildHouseCd = 0;

        buildHouseCd--;

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
    }
