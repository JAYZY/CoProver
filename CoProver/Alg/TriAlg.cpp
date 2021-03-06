/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   TriAlg.cpp
 * Author: Zhong Jian<77367632@qq.com>
 * 
 * Created on 2018年10月25日, 上午10:46
 */

#include "TriAlg.h"
#include "CLAUSE/LiteralCompare.h"
#include "HEURISTICS/SortRule.h"
#include "Inferences/Simplification.h"
#include "Inferences/InferenceInfo.h"
#include <set>
#include <algorithm>
#include <bits/stl_set.h>

TriAlg::TriAlg(Formula* _fol) : fol(_fol), subst(nullptr) {
    subst = new Subst();

    iniVect();
}

TriAlg::TriAlg(const TriAlg& orig) {
}

TriAlg::~TriAlg() {
    DelPtr(subst);
    disposeRNUnitCla();

}


/// 预处理模块== 将所有二元子句 全部与单元子句进行合一生成单元子句
/// \param givenCla
/// \return 

RESULT TriAlg::GenByBinaryCla(Clause* binaryCla) {
    /*
     * 1.传入目标子句 -- 二元子句,所有文字与单元子句进行合一       
     */
    //限制为二元子句

    iniVect();
    subst = new Subst();
    Literal* checkedLit = nullptr; //检查过的文字_+
    if (binaryCla->LitsNumber() > 2)
        return RESULT::NOCLAUSE;
    bool isUnify = false;
    //debug --  
    binaryCla->ClausePrint(stdout, true);
    cout << endl;
    for (Literal* litp = binaryCla->literals; litp; litp = litp->next) {
        vector<Clause* >&cmpUnitClas = litp->IsPositive() ? fol->vNegUnitClas : fol->vPosUnitClas; //可以考虑优化 用索引树

        //------ 遍历单元子句 ------
        for (int ind = 0; ind < cmpUnitClas.size(); ++ind) {
            Clause* candUnitCal = cmpUnitClas[ind];
            //被删除的单元子句 不处理  
            if (candUnitCal->isDel()) {
                continue;
            }
            Lit_p candLit = candUnitCal->Lits();
            //谓词符号不同
            if (!litp->EqnIsEquLit() && litp->lterm->fCode != candLit->lterm->fCode)
                continue;
            //回退点    
            int backpoint = subst->Size();
            //合一检查
            if (unify.literalMgu(litp, candLit, subst)) {
                if (vNewR.empty()) { //第一次合一成功
                    //1.检查剩余文字冗余情况
                    Lit_p holdLit = (nullptr == checkedLit) ? litp->next : checkedLit;
                    if (fol->unitLitIsRundacy(holdLit)) {
                        subst->SubstBacktrackToPos(backpoint);
                        continue;
                    }
                    //3.添加主界线文字
                    vALitTri.push_back(new ALit{0, -1, candLit, litp});
                    //3.添加剩余文字
                    vNewR.push_back(holdLit);
                    //4.记录合一成功
                    isUnify = true;
                } else {
                    vNewR.clear();
                    //5.输出主界线和空R信息
                    this->outTri();
                    this->outR(nullptr);
                    //6.返回 UNSAT
                    return RESULT::UNSAT;
                }

            } else {//合一失败,回退替换,继续查找
                subst->SubstBacktrackToPos(backpoint);
            }
        }
        //记录检查过的文字
        checkedLit = litp;
    }
    //检查完成,输出R,添加新单元子句
    if (isUnify) {
        assert(1 == vNewR.size());
        this->outTri();
        this->outR(nullptr);
        Clause* newCla = new Clause();
        Literal* newLitP = vNewR[0]->RenameCopy(newCla);
        newCla->bindingLits(newLitP);
        fol->insertNewCla(newCla); //直接加入单元子句
        //输出到.i 文件
        outNewClaInfo(newCla, InfereType::SCS);
    }
    return RESULT::SUCCES;
}

/*-----------------------------------------------------------------------
 *算法流程：
 * 1. 选择子句givenCla 
 * 2. 对子句进行单元子句归结
 * 3. 选择
 * 
/*---------------------------------------------------------------------*/
//

RESULT TriAlg::GenreateTriLastHope(Clause * givenCla) {

    string strOut = "# 起步子句C" + to_string(givenCla->ident) + ":";
    givenCla->getStrOfClause(strOut);
    FileOp::getInstance()->outRun(strOut);
    //Print-level      
    cout << strOut << endl;

    iniVect();
    subst->Clear();
    RESULT resTri = RESULT::NOMGU;
    //子句中文字的排序策略;文字的使用次数和发生冗余的次数会影响它的排序
    givenCla->SortLits();

    //经过多次合一，构建三角形
    Lit_p actLit = nullptr, pasLit = nullptr; //主动/被动归结文字

    uint16_t uActHoldLitNum = givenCla->LitsNumber(); //记录主动归结子句的剩余文字个数
    uint16_t uPasHoldLitNum = 0; //记录被动归结子句的剩余文字个数
    //回退相关
    vector<uint32_t> vRecodeBackPoint; //替换的回退点,每次成功一个配对 就记录一次 
    vRecodeBackPoint.reserve(32);
    vector<uint32_t> vPasCandBackPoint; //候选文字集序号的回退点
    vPasCandBackPoint.reserve(32);

    vector<Literal*>* vCandLit = nullptr; //候选文字集合
    Clause* actCla = givenCla; //记录主动子句

    int backpoint = 0; //合一替换的回退点
    uint32_t pasLitInd = 0; //被动归结文字序号
    bool isDeduct = false; //是否发生过归结

    /************************************************************************/
    /*主动文字选择原则 ,1 尽量选择 负文字 2 尽量选择稳定度低的文字	
    /************************************************************************/
    setUsedCla.insert(givenCla); //记录起步子句已经使用.
    //对选择的子句 进行 单文字匹配
    actLit = actCla->literals;
    while (actLit) {
        //对第一次参与归结的文字,修改属性为Hold
        actLit->EqnSetProp(EqnProp::EPIsHold);
        actLit = actLit->next;
    }
    actLit = actCla->literals;

    while (1) {
        //对主动子句 A.单文字匹配 B.确定起步文字actLit
        if (actLit) {
            //检查项的嵌套深度
            if (-1 == actLit->lterm->CheckTermDepthLimit()) {
                actLit = actLit->next;
                continue;
            }
            //对主动子句中的剩余文字进行单文字匹配
            if (unitResolutionrReduct(&actLit, uActHoldLitNum)) {
                isDeduct = true;
            }

            //主动归结子句,被单元子句约减后没有剩余文字。三角形停止延拓,并输出主界线和R(注意此时R并不一定为空)
            if (actLit == nullptr) {
                // this->printTri(stdout);                 this->printR(stdout, nullptr);

                Clause* newCla = new Clause();
                newCla->bindingAndRecopyLits(vNewR);
                //newClas.push_back(newCla);

                // ------ 对子句进行下拉约减 ------
                {
                    int delRNum = TriMguReduct();
                    if (delRNum > 0) {
                        Clause* newClaA = new Clause();
                        newClaA->bindingAndRecopyLits(vNewR);
                        newClas.push_back(newClaA);
                        FileOp::getInstance()->outLog("# UR 继续约减.C" + to_string(newClaA->ident) + "合一下拉:" + to_string(delRNum) + "个文字\n");

                        if (StrategyParam::ADD_CR) { //若策略 允许保留原始子句，则下拉越减前的子句加入子句集。否则删除
                            newClas.push_back(newCla);
                        } else {
                            DelPtr(newCla);
                        }
                    } else {
                        newClas.push_back(newCla);
                    }
                }
                return RESULT::SUCCES;
            }
            //只剩一个文字 -- 单元子句
            if (isDeduct && 0 == vNewR.size()&& 1 == uActHoldLitNum) {
                this->outTri();
                this->outR(actLit);
                Clause* newCla = new Clause();
                assert(actLit->IsHold());
                Literal* newLitP = actLit->RenameCopy(newCla);
                newCla->bindingLits(newLitP);
                fol->insertNewCla(newCla); //直接加入单元子句
                //输出到.i 文件
                outNewClaInfo(newCla, InfereType::SCS);
                //一旦 有剩余文字是单元子句,若该单元子句 找不到拓展的文字,则回退
                isDeduct = false; //合一&规则检查成功
            }
        }

        //上一轮的被动子句,新一轮的主动子句, 已经被主界线下拉过, 又被单元子句下拉 则判断剩余文字R是否超出限制,是否需要停止三角形演绎
        if (actCla != givenCla && 0 < StrategyParam::MaxLitNumOfR && vNewR.size() + uActHoldLitNum > StrategyParam::MaxLitNumOfR) {
            // string strOverMaxLitLimitNum = to_string(vNewR.size() + uActHoldLitNum) + " 超出次数:" +to_string(++StrategyParam::S_OverMaxLitLimit_Num) + "\n";
            // FileOp::getInstance()->outLog("R长度不符合要求:当前R长度:" +strOverMaxLitLimitNum);
            actLit = nullptr;
        }

        //=======遍历主动子句中剩余文字 -----------------------------------------------------
        while (actLit) {

            //根据文字互补谓词项对应的互补文字得到候选被动文字
            vCandLit = fol->getPairPredLst(actLit);

            /* 对候选被动文字进行排序 A.被动文字所在子句文字数从少到多;B.相同文字数情况下考虑稳定度,主动被动相近度 等启发式策略[u]如何高效的排序?*/
            if (resTri != RESULT::RollBack)
                stable_sort(vCandLit->begin(), vCandLit->end(), SortRule::PoslitCmp);

            //遍历候选被动文字子句顺序进行匹配查找 -----------------------------------------------------
            for (; pasLitInd < vCandLit->size(); ++pasLitInd) {

                resTri = RESULT::NOMGU;
                pasLit = vCandLit->at(pasLitInd); //候选被动归结文字               
                //==========候选文字的条件限制==================
                {
                    if (pasLit->claPtr->ClauseQueryProp(ClauseProp::CPDeleteClause))//若当前子句被删除,注意后续优化 若文字所在子句被删除,则在排序的时候 就可以删除掉
                        continue;

                    /*限制子句中文字数个数 剩余R+主动子句剩余文字数+候选子句文字数-2<=limit*/
                    uPasHoldLitNum = pasLit->claPtr->LitsNumber() - 1;
                    //                    if (0 < StrategyParam::HoldLits_NUM_LIMIT && vNewR.size() + uActHoldLitNum + uPasHoldLitNum > StrategyParam::HoldLits_NUM_LIMIT) {
                    //                        pasLit->usedCount += StrategyParam::LIT_OVERLIMIT_WIGHT;
                    //                        ++Env::S_OverMaxLitLimit_Num;
                    //                        continue;
                    //                    }
                    /*同一子句中文字不进行比较;归结过的子句不在归结;文字条件限制*/
                    if (pasLit->claPtr == actLit->claPtr || setUsedCla.find(pasLit->claPtr) != setUsedCla.end())
                        continue;
                    //不允许跟上一轮母式进行归结  
                    if ((pasLit->parentLitPtr && pasLit->parentLitPtr->claPtr == actLit->claPtr)
                            || (actLit->parentLitPtr && actLit->parentLitPtr->claPtr == pasLit->claPtr))
                        continue;
                    //参与归结文字 函数嵌套层不能超过限制[必须优化]
                    if ((-1 == actLit->lterm->CheckTermDepthLimit()) || (-1 == pasLit->lterm->CheckTermDepthLimit()))
                        break;

                }

                //================== 合一操作 ==================
                backpoint = subst->Size();
                bool res = unify.literalMgu(actLit, pasLit, subst);
                if (!res) {
                    subst->SubstBacktrackToPos(backpoint);
                    continue;
                }

                actLit->EqnDelProp(EqnProp::EPIsHold);
                //================== 规则检查 ==================
                //1.规则检查 -----------------------------------
                //ResRule resRule = this->RuleCheckLastHope(actLit);
                bool isVarChg = subst->Size() > backpoint;
                ResRule resRule = this->RuleCheckOri(actLit, pasLit, uPasHoldLitNum, isVarChg);
                if (resRule == ResRule::ChgActLit) {//换主界线文字
                    subst->SubstBacktrackToPos(backpoint);
                    actLit->EqnSetProp(EqnProp::EPIsHold);
                    break;
                } else if (resRule == ResRule::ChgPasLit) {//换被归结文字
                    subst->SubstBacktrackToPos(backpoint);
                    continue;
                } else if (resRule == ResRule::RSubsump) { //冗余 改变参数?
                    pasLit->usedCount += 2;
                    subst->SubstBacktrackToPos(backpoint);
                    continue;
                } else if (resRule == ResRule::MoreLit) {
                    pasLit->usedCount += StrategyParam::LIT_OVERLIMIT_WIGHT;
                    subst->SubstBacktrackToPos(backpoint);
                    continue;
                }

                //==================三角形构建成功,后续处理 ====================
                {
                    //-------------------------------------
                    isDeduct = true; //合一&规则检查成功
                    //1.添加主界线文字
                    //actLit->EqnDelProp(EqnProp::EPIsHold);
                    vALitTri.push_back(new ALit{0, -1, actLit, pasLit});
                    ++actLit->usedCount; //文字使用次数+1

                    //2.添加回退点--替换栈
                    vRecodeBackPoint.push_back(backpoint);

                    //3.添加回退点--候选文字序号
                    vPasCandBackPoint.push_back(pasLitInd);

                    //5.记录 已经使用的被动子句. 
                    setUsedCla.insert(pasLit->claPtr);
                    ++pasLit->usedCount; //文字使用次数+1

                    //6.判断若剩余文字为单文字则构成新子句 添加到新子句中==================
                    if (vNewR.empty()&& 1 == uPasHoldLitNum) {
                        //输出
                        // this->printTri(stdout);                        this->printR(stdout, pasLit->claPtr->literals);
                        this->outTri();
                        this->outR(pasLit->claPtr->literals);
                        Clause* newCla = this->getNewCluase(pasLit->claPtr);
                        fol->insertNewCla(newCla);
                        //输出到.i 文件
                        outNewClaInfo(newCla, InfereType::SCS);
                        //一旦有剩余文字是单元子句,若该单元子句 找不到拓展的文字,则回退
                        isDeduct = false; //合一&规则检查成功
                    }
                    resTri = RESULT::SUCCES;
                }

                break;
            }
            if (resTri == RESULT::SUCCES)
                break;
            //====== 换主动文字 -- 候选被动匹配失败 -----------------------------------------------------
            actLit->EqnSetProp(EqnProp::EPIsHold);
            actLit = actLit->next;
            while (actLit != nullptr&&!actLit->EqnQueryProp(EqnProp::EPIsHold)) { // 没有被left 继续下一个
                actLit = actLit->next;
                pasLitInd = 0;
            }

            resTri = RESULT::NOMGU;
        }

        /*======== △无法延拓时候进行处理 ====================*/
        if (resTri == RESULT::NOMGU) {
            /*注意此时有两种情况：A.已经成功构建 △。B.选择的*/

            //==================== △构建成功 ====================
            if (isDeduct) {
                //1.将主动文字的剩余文字添加到 vNewR中 
                for (Lit_p litptr = actCla->literals; litptr; litptr = litptr->next) {
                    if (litptr->EqnQueryProp(EqnProp::EPIsHold))
                        vNewR.push_back(litptr);
                }
                // this->printTri(stdout);                this->printR(stdout, nullptr);

                Clause* newCla = new Clause();
                newCla->bindingAndRecopyLits(vNewR);

                bool isDelNewCla = true;
                int delRNum = TriMguReduct();
                if (delRNum > 0) {
                    // this->printTri(stdout);                    this->printR(stdout, nullptr);
                    Clause* newClaA = new Clause();
                    newClaA->bindingAndRecopyLits(vNewR);
                    newClas.push_back(newClaA);
                    FileOp::getInstance()->outLog("# R 继续约减.C" + to_string(newClaA->ident) + "合一下拉:" + to_string(delRNum) + "个文字\n");
                } else {
                    newClas.push_back(newCla);
                    isDelNewCla = false;
                }
                if (isDelNewCla) {
                    if (StrategyParam::ADD_CR) {
                        newClas.push_back(newCla);
                    } else {
                        DelPtr(newCla);
                    }
                }
                return RESULT::SUCCES;
            }//====== △起步后，没有任何延拓 则进行回退操作 ======
            else {
                /*
                 * 回退的类型:
                 * 1.主对角线重新查找下一个互补文字(改变被归结文字);2.重新选择主界线文字(改变主动归结文字);3.重新选择被下拉的主界线文字(改变下拉替换)
                 回退分析: 
                 * A 在正常△构建中,从主动文字出发,无法找到Linked文字，则需要尝试主动子句中其他剩余文字。--- 在构建△过程中完成.即，改变归结文字不属于路径回退
                 * B 在正常△构建中,△无法延拓。则主动子句Ci+1 退回到Ci子句重新使用 Li子句查找其他匹配文字。 主动文字为主界线最后一个文字。~Li+1 为不能归结文字（记录位置下标）
                 *  注意：处理流程  被动子句 1）先被主动文字匹配确定被动文字~Li+1, 2）主界线文字下拉；3）单元子句下拉
                 * 难点：此时主动子句Ci已经经历过主界线 已经经历过：主界线文字下拉，单元子句下拉操作，因此回退的时候需要
                 * 
                 * 剩余文字均无法找到Linked文字,此时需要回退.
                 * 若某个主动文字均找不到可以延拓的文字,则说明,类型1(改变被归结文字)不适用. 
                 * 此时考虑两种情况 检查是否有下拉,若有下拉,则重新下拉并且还是重该主界线文字出发
                 */
                //1.主对角线重新查找下一个互补文字(改变被归结文字);
                if (vALitTri.empty() || (actCla == givenCla)) { //已经没有回退点了
                    return RESULT::NOMGU;
                }
                //-----------------  回退操作 --------------------------------//
                // cout << "回滚前:";                printTri(stdout);                this->printR(stdout, nullptr);

                actLit = vALitTri.back()->alit; //1.设置主动文字==主界线最后一个文字,并且主界线pop(回退)

                actLit->EqnSetProp(EqnProp::EPIsHold);

                vALitTri.pop_back();

                //2.回退单元子句下拉
                while ((!vALitTri.empty()) && vALitTri.back()->blit->claPtr == actCla) {
                    vALitTri.back()->alit->EqnSetProp(EqnProp::EPIsHold);
                    setUsedCla.erase(vALitTri.back()->alit->claPtr); //删除使用的子句 
                    vALitTri.pop_back();
                }

                //3.修改其他主界线下拉次数 
                while ((!vReduceLit.empty()) && vReduceLit.back()->cLit->claPtr == actCla) {
                    vReduceLit.back()->aLit->reduceNum--;
                    vReduceLit.pop_back();
                }
                setUsedCla.erase(actCla); //删除使用的子句 


                //回退vNewR
                while ((!vNewR.empty()) && (vNewR.back()->claPtr == actCla)) {
                    Lit_p lit = (vNewR.back());
                    lit->EqnSetProp(EqnProp::EPIsHold);
                    vNewR.pop_back();
                }
                actCla = actLit->claPtr;
                uActHoldLitNum = actLit->claPtr->LitsNumber() - 1;
                //回退 合一替换
                if (!vRecodeBackPoint.empty()) {
                    uint32_t backPoint = vRecodeBackPoint.back();
                    vRecodeBackPoint.pop_back();
                    subst->SubstBacktrackToPos(backPoint);
                }
                //设置候选文字下标
                pasLitInd = 0;
                if (!vPasCandBackPoint.empty()) {
                    pasLitInd = vPasCandBackPoint.back() + 1;
                    vPasCandBackPoint.pop_back();
                    resTri = RESULT::NOMGU;
                }
                //   cout << "回滚后:";                printTri(stdout);                this->printR(stdout, nullptr);
                continue;
            }
            //assert(resTri == RESULT::SUCCES);
        }/*======== △延拓成功   ==============================*/
        else {
            //5.交换主动被动子句,进行△延拓
            uActHoldLitNum = uPasHoldLitNum;
            uPasHoldLitNum = 0;
            pasLitInd = 0;
            //得到被动子句中,下一个剩余文字
            actCla = pasLit->claPtr;
            actLit = actCla->Lits();
            pasLit = nullptr;
            while (actLit != nullptr&&!actLit->EqnQueryProp(EqnProp::EPIsHold)) {
                //没有被left 继续下一个
                actLit = actLit->next;
            }
            resTri = RESULT::NOMGU;
        }
    }
    return RESULT::SUCCES;
}

/// 规则检查
/// \param actLit
/// \param candLit
/// \return 

ResRule TriAlg::RuleCheck(Literal*actLit, Literal* candLit, Lit_p *leftLit, uint16_t & uLeftLitNum) {
    /*规则检查 ，
     * 总的原则:  主界线上文字不能相同(互补);剩余文字不能相同(恒真);
     * 一.主界线(A)文字与前面剩余(B)文字不能相同 [不做合一].
     * 1.检查主动文字与前面剩余文字相同 [ 直接换主动文字 ]
     * 2.其它主界线文字与前面B文字相同 [ 说明由于替换导致相同,换下一个被动归结文字 ]
     * 3.主动文字与前面主界线文字相同 [直接换主动文字]
     * 二.对被动归结子句中剩余文字
     * 4.检查与对角线文字互补 [删除(下拉)] -- * 注意需要合一,优先做检查 * 
     * 
     * 5.检查被动文字与对角线文字相同[ 换下一个主动文字 ] -- 不允许主界线文字相同(不考虑合一)
     * 
     * 三.检查剩余文字
     * 1.剩余文字之间是否恒真； [ 换下一个被动归结文字 ]
     * 2.检查与前面剩余文字恒真 [ 换下一个被动归结文字 ]
     * 3.检查与前面剩余文字相同 [ 删除(合并) ]
     *  
     * 四.对剩余文字用单元文字合一(下拉)
     */
    Unify unify;
    Cla_p candCla = candLit->claPtr;

    assert(uLeftLitNum == 0);
    bool isLeftLit = true;
    //优先完成合一操作 -- 
    //==== 遍历被动归结子句的剩余文字 1.单元子句[合一下拉];2.主界线文字[合一下拉] =====================//  
    Lit_p bLit = candCla->Lits();
    for (; bLit != nullptr; bLit = bLit->next) {
        bLit->EqnDelProp(EqnProp::EPIsHold);
        if (bLit == candLit) {
            continue;
        }

        /* 用单元集子句下拉剩余文字 -- 注意失败后需要 还原*/
        if (unitResolutionBySet(bLit)) {
            cout << "单文字下拉" << endl;
            continue;
        }

        isLeftLit = true;
        /* 检查与对角线文字互补下拉,注意:不检查与actLit的合一互补情况,这个涉及factor内容 */
        for (ALit* elem : vALitTri) {
            Lit_p aLit = elem->alit;

            int backpointA = subst->Size();
            if (bLit->isComplementProps(aLit) && unify.literalMgu(bLit, aLit, subst)) {
                /* 主界线文字下拉(合一)剩余文字.并记录下拉次数[要保留合一替换的内容]  */
                ++(elem->reduceNum);
                vReduceLit.push_back(new RLit{bLit, elem}); //记录下拉文字
                //++uReduceNum;
                isLeftLit = false;
                // cout << "主界线下拉" << endl;
                break;
            } else {
                subst->SubstBacktrackToPos(backpointA);
            }
        }

        if (isLeftLit) {
            //添加到剩余文字标志
            bLit->EqnSetProp(EqnProp::EPIsHold);
            // leftLit[uLeftLitNum++] = bLit;
        }

    }

    //====遍历主界线(A文字) 确保主界线文字不能相同=====================//   
    vector<ALit_p>::iterator iterALit = vALitTri.begin();
    /*"输出测试 -主界线:" */

    {

        //        cout << "输出测试 -主界线:" << endl;
        //        fprintf(stdout, "actLit[C%u_%d]", actLit->claPtr->ident, actLit->pos);
        //        actLit->EqnTSTPPrint(stdout, true);
        //        cout << endl;
        //        while (iterALit != vALitTri.end()) {
        //            Lit_p aLit = (*iterALit)->alit;
        //            fprintf(stdout, "测试[C%u_%d]", aLit->claPtr->ident, aLit->pos);
        //            aLit->EqnTSTPPrint(stdout, true);
        //            cout << endl;
        //            Lit_p bLit = (*iterALit)->blit;
        //            fprintf(stdout, "测试[C%u_%d]", bLit->claPtr->ident, bLit->pos);
        //            bLit->EqnTSTPPrint(stdout, true);
        //            cout << endl;
        //            ++iterALit;
        //        }
        //        cout << "\n------主界线------" << endl;
        //        iterALit = vALitTri.begin();
    }

    while (iterALit != vALitTri.end()) {

        Lit_p aLitPtr = (*iterALit)->alit;
        if (actLit->equalsStuct(aLitPtr)) { //当前选择的主动文字与主界线相同时候
            //输出测试--------------
            {
                //                fprintf(stdout, "\n[C%u_%d]", actLit->claPtr->ident, actLit->pos);
                //                actLit->EqnTSTPPrint(stdout, true);
                //                cout << endl;
                //
                //                fprintf(stdout, "\n[C%u_%d]", aLitPtr->claPtr->ident, aLitPtr->pos);
                //                aLitPtr->EqnTSTPPrint(stdout, true);
                //                cout << endl;
            }

            assert(actLit->isSameProps(aLitPtr)); //主界线文字不存在互补情况

            return ResRule::ChgActLit;
        }
        vector<ALit_p>::iterator iterALitB = iterALit + 1;
        while (iterALitB != vALitTri.end()) {
            Lit_p aLitB = (*iterALitB)->alit;
            if (aLitPtr->equalsStuct(aLitB)) {
                assert(aLitPtr->isSameProps(aLitB)); //如果互补上一个检查应该下拉
                return ResRule::ChgActLit;
            }
            ++iterALitB;
        }
        ++iterALit;
    }


    //====遍历剩余文字(恒真,合并) ============================//    
    vector<Lit_p>::iterator iterA = vNewR.begin();
    for (; iterA != vNewR.end(); ++iterA) {
        Lit_p bElem = *iterA;

        //检查主动文字与前面剩余文字相同(直接换主动文字) -- 注意这种相同往往是由于 合一替换导致的
        if (candLit->isComplementProps(bElem) && candLit->equalsStuct(bElem)) {
            return ResRule::ChgActLit;
        }
        /* 主界线文字与前面剩余文字 [不能相同(不做合一)]. [U]这里暂时不考虑factor情况.  */
        //[U]这里可以考虑采用索引方式 进行优化        
        for (ALit* aElem : vALitTri) {
            if (bElem->isComplementProps(aElem->alit) && bElem->equalsStuct(aElem->alit)) {
                //其他主界线文字与前面B文字相同(说明相同是由于替换产生的,换下一个被动归结文字)
                return ResRule::ChgPasLit;
            }
        }
        vector<Lit_p>::iterator iterB = iterA + 1;
        /* 前面剩余文字之间 [恒真(换下一个被动归结文字)].[相同(删除后面的剩余文字)]*/
        while (iterB != vNewR.end()) {
            Lit_p bElemB = *iterB;
            if (bElem->equalsStuct(bElemB)) {
                if (bElem->isComplementProps(bElemB)) {//剩余文字恒真,回退下一个被动归结文字
                    return ResRule::ChgPasLit;
                } else if (bElem->isComplementProps(bElemB)) { //剩余文字相同,删除后面的剩余文字
                    iterB = vNewR.erase(iterB);
                    continue;
                }
            }
            ++iterB;
        }
    }



    bool isDelLit = false;

    //==== 再一次遍历被动归结子句的剩余文字 ======================//
    uLeftLitNum = 0;
    bLit = candCla->Lits();
    for (; bLit != nullptr; bLit = bLit->next) {
        if (!bLit->EqnQueryProp(EqnProp::EPIsHold)) {
            continue;
        }
        isDelLit = false;
        bLit->EqnDelProp(EqnProp::EPIsHold);
        assert(bLit != candLit);

        //====检查剩余文字与被归结文字相同(删除)======================//
        if (bLit->isComplementProps(actLit) && bLit->equalsStuct(actLit)) {
            isDelLit = true;
            continue;
        }

        //====检查剩余文字(B文字)与前面剩余文字是否(恒真,相同)======================//
        for (Lit_p bElem : vNewR) {
            //[U]这里暂时不考虑  factor情况.
            // 检查被动子句中的文字 与剩余文字是否 相同(合并),采用完全相同,若采用合一相同,则是不完备的(factor),可以考虑不同策略
            if (bLit->equalsStuct(bElem)) {
                if (bLit->isComplementProps(bElem)) {//谓词符号互补,恒真(换下一个被动归结文字)

                    return ResRule::ChgPasLit;
                }
                assert(bLit->isSameProps(bElem)); //谓词符号相同,合并 
                isDelLit = true;
                break;
            }
        }
        if (isDelLit) continue;
        assert(!isDelLit);
        bLit->EqnSetProp(EqnProp::EPIsHold);
        leftLit[uLeftLitNum++] = bLit;

    }

    return ResRule::RULEOK;


}

//原始的规则检查

ResRule TriAlg::RuleCheckOri(Literal*actLit, Literal* candLit, uint16_t& uPasClaHoldLitSize, bool isVarChg) {
    /*规则检查 ，
     ** 总的原则:  A主界线上文字不能相同或合一互补; B.主界线与前面剩余文字不能相同;C. 剩余文字R不能恒真;
     ** 检查:
     ** 1. 对被动子句检查 
     ** 1.1 被动文字,与主界线文字相同                [换主动文字] 不换被动文字的原因,有可能主动文字经过替换后可以下拉消除掉
     ** 1.2 被动文字,与主界线文字互补                [换被动文字] PS,说明主动文字与主界线文字相同了
     ** 1.3 被动文字,不与R文字互补                   [换主动文字] PS.这些都是因为替换导致的相同    
     ** 1.4 被动子句中其他文字,相互之间不能互补      [换被动文字]
     ** 1.5 被动子句中其他文字,与R文字不能互补       [换被动文字]
     
     ## 1.6 被动子句中其他文字,相互之间不能相同    [记录下来,统一删除].    
     ## 1.7 被动子句中其他文字,与R文字不能相同     [记录下来,统一删除].
     ## 1.8 被动子句中其他文字与主界线文字互补     [记录下拉,删除(下拉)] 
     ## 1.9 被动子句中其他文字与主界线文字相同     [记录下来,不能作为下次归结主动文字]
     * 
     * 思考? 若被动文字与剩余文字R相同会怎么样(主动文字与R互补) --  允许???
     * 算法:检查每个文字是否与主界线文字相同或互补; 检查每个文字是否与R文字相同或互补; 检查每个文字相互之间相同或互补
     
     ** 2. 对主动子句检查,
     ** 2.1 检查主动文字,不与主界线文字相同/不与R文字相同  PS:若被动文字做检查, 则主动文字不做检查.反之已然
     ** 2.2 主动子句中的其他文字相互之间不能互补    [换主动文字] PS.合一导致的
     ** 2.3 主动子句中的其他文字与R文字不能互补     [换主动文字] PS.合一导致的
     
     ## 2.4 主动子句中的其他文字相互之间不能相同    [记录下来,统一删除].
     ## 2.5 主动子句中的其他文字与R文字不能相同     [记录下来,统一删除].
     ## 2.6 主动子句中的其他文字与对角线文字互补    [记录下拉,删除(下拉)] 
       
     ** 3.对主界线(A)文字检查 PS.若没有合一替换发生,则不需要检查[优化]
     ** 3.1 相互之间不能相同/互补                [换被动文字] PS:可以再考虑一下这个互补的问题.
     ** 3.2 与前面R中文字不能相同                [换被动文字] 
    
     ** 4.对R文字检查 PS.若没有合一替换发生,则不需要检查[优化]
     ** 4.1 R中的文字互补                        [换被动文字] 
     ** 4.2 R中的文字相同                        [记录下来,统一删除].
     ** 4.3 R中的文字与该子句中的主界线文字相同  [记录下来,统一删除].
      
     ** 5.检查主动子句剩余文字+被动子句剩余文字+R 是否是 forwardsubsume [换被动文字/子句] 
     */
    Clause* pasCla = candLit->claPtr;
    Clause* actCla = actLit->claPtr;
    uint16_t holdLitSize = 0; //剩余文字总个数

    uint16_t uActClaHoldLitSize = actCla->LitsNumber();

    uPasClaHoldLitSize = pasCla->LitsNumber() - 1;

    Literal * holdLit[uPasClaHoldLitSize + uActClaHoldLitSize + vNewR.size() - 1]; //创建数组用来存储 数据 [注意优化]

    vector<Literal*> noSelActLit; //测试- 被动子句中不能作为下次主动文字

    vector<Literal*> vDelLit; //删除的文字
    char arryDelRInd[vNewR.size()]; //删除的R中文字编号
    memset(arryDelRInd, 0, sizeof (arryDelRInd)); //全部初始化为0;

    bool isHold = true;
    //----- 优先检查被动子句 ------// 
    for (Literal* pLit = pasCla->literals; pLit; pLit = pLit->next) {
        isHold = true;
        //注意:由于被动子句中的文字第一次使用因此必须还原为IsHold属性(除了被动文字)
        pLit->EqnSetProp(EqnProp::EPIsHold);
        //----- 遍历R中的文字 ------// 
        for (Literal* pLitR : vNewR) {
            if (pLit->equalsStuct(pLitR)) {
                //互补
                if (pLit->isComplementProps(pLitR)) {
                    //1.3 被动文字,与R文字互补                 [换主动文字]
                    if (pLit == candLit)
                        return ResRule::ChgActLit;
                    else//1.5 被动子句中其他文字,与R文字互补   [换被动文字--剩余文字恒真] 
                        return ResRule::ChgPasLit;
                }//1.7 被动子句中其他文字,与R文字不能相同     [记录下来,统一删除].
                else if (pLit != candLit) {
                    isHold = false;
                    vDelLit.push_back(pLit);
                    break;
                }
            }
        }
        if (!isHold) {
            assert(uPasClaHoldLitSize > 0);
            --uPasClaHoldLitSize;
            continue;
        }
        //----- 遍历主界线中的文字 ------// 
        for (ALit* pLitTri : vALitTri) {
            Literal* alitTri = pLitTri->alit;
            if (pLit->equalsStuct(alitTri)) {
                //互补
                if (pLit->isComplementProps(alitTri)) {
                    //1.2 被动文字与主界线文字互补                [换被动文字] PS,说明主动文字与主界线文字相同了
                    if (pLit == candLit)
                        return ResRule::ChgPasLit;
                    else { //1.8 被动子句中其他文字与主界线文字互补   [记录下拉,删除(下拉)] 
                        isHold = false;
                        vDelLit.push_back(pLit);
                        break;
                    }
                }//相同
                else {
                    //1.1 被动文字与主界线文字相同(主动文字也许可以下拉) [换主动文字] 
                    if (pLit == candLit)
                        return ResRule::ChgActLit;
                    else {//1.9 被动子句中其他文字与主界线文字相同     [记录下来,不能作为下次归结主动文字]
                        noSelActLit.push_back(pLit);
                        break;
                    }
                }
            }
        }

        if (!isHold) {
            assert(uPasClaHoldLitSize > 0);
            --uPasClaHoldLitSize;
            continue;
        }
        if (pLit == candLit)
            continue;

        if (pLit->equalsStuct(candLit)) {
            //1.4 被动子句中其他文字,相互之间不能互补--专门检查与被动文字之间         [换被动文字]
            if (pLit->isComplementProps(candLit)) {
                ResRule::ChgPasLit;
            } else {//1.6 被动子句中其他文字,相互之间不能相同--专门检查与被动文字之间 [记录下来,统一删除]. 
                isHold = false;
                vDelLit.push_back(pLit);
            }
        }
        if (!isHold) {
            assert(uPasClaHoldLitSize > 0);
            --uPasClaHoldLitSize;
            continue;
        }
        //----- 遍历被动子句中的其他文字 ------// 
        for (Literal* pLitB = pLit->next; pLitB; pLitB = pLitB->next) {
            if (pLitB == candLit)continue;
            if (pLit->equalsStuct(pLitB)) {
                //1.4 被动子句中其他文字,相互之间不能互补      [换被动文字]
                if (pLit->isComplementProps(pLitB)) {
                    ResRule::ChgPasLit;
                }//相同 
                else {//1.6 被动子句中其他文字,相互之间不能相同    [记录下来,统一删除]. 
                    isHold = false;
                    vDelLit.push_back(pLit);
                    break;
                }
            }
        }
        if (isHold)
            holdLit[holdLitSize++] = pLit;
        else {
            assert(uPasClaHoldLitSize > 0);
            --uPasClaHoldLitSize;
            continue;
        }
    }


    //----- 检查主动子句 ------// 
    for (Literal* aLit = actCla->literals; aLit; aLit = aLit->next) {
        //已经删除的文字不考虑
        if (!aLit->EqnQueryProp(EqnProp::EPIsHold) || aLit == actLit) {
            --uActClaHoldLitSize;
            continue;
        }
        isHold = true;
        //剩余文字与主动归结文字相同/互补
        if (aLit->equalsStuct(actLit)) {
            //2.2 主动子句中的其他文字,相互之间不能互补--特别检查主动归结文字 [换主动文字] PS.合一导致的
            if (aLit->isComplementProps(actLit)) {
                return ResRule::ChgActLit;
            }//2.4 主动子句中的其他文字相互之间不能相同--特别检查主动归结文字[记录下来,统一删除].
            else {
                isHold = false;
                vDelLit.push_back(aLit);
            }
        }
        if (!isHold) {
            --uActClaHoldLitSize;
            continue;
        }
        //----- 遍历主动子句中的其他文字 ------// 
        for (Literal* aLitB = aLit->next; aLitB; aLitB = aLitB->next) {
            //已经删除的文字不考虑
            if (!aLitB->EqnQueryProp(EqnProp::EPIsHold) || aLitB == actLit)
                continue;
            if (aLit->equalsStuct(aLitB)) {
                //2.2 主动子句中的其他文字,相互之间不能互补     [换主动文字] PS.合一导致的
                if (aLit->isComplementProps(aLitB)) {
                    return ResRule::ChgActLit;
                }//2.4 主动子句中的其他文字相互之间不能相同     [记录下来,统一删除].
                else {
                    isHold = false;
                    vDelLit.push_back(aLit);
                    break;
                }
            }
        }

        if (!isHold) {
            --uActClaHoldLitSize;
            continue;
        }
        //----- 遍历R中的文字 ------// 
        for (Literal* pLitR : vNewR) {
            if (aLit->equalsStuct(pLitR)) {
                //2.3 主动子句中的其他文字,不与R文字互补    [换主动文字] PS.合一导致的
                if (aLit->isComplementProps(pLitR)) {
                    return ResRule::ChgActLit;
                }//2.5 主动子句中的其他文字与R文字不能相同   [记录下来,统一删除]
                else {
                    isHold = false;
                    vDelLit.push_back(aLit);
                    break;
                }
            }
        }
        //----- 遍历主对角线中的文字 ------// 
        for (ALit_p pLitTri : vALitTri) {
            Literal* alitTri = pLitTri->alit;
            //2.6 主动子句中的其他文字与对角线文字互补  [记录下拉,删除(下拉)] 
            if (aLit->isComplementProps(alitTri) && aLit->equalsStuct(alitTri)) {
                isHold = false;
                vDelLit.push_back(aLit);
                break;
            }
        }
        if (isHold)
            holdLit[holdLitSize++] = aLit;
        else {
            --uActClaHoldLitSize;
            continue;
        }

    }

    //    /*限制子句中文字数个数 剩余R+主动子句剩余文字数+候选子句文字数-2<=limit*/
    //    if (0 < StrategyParam::MaxLitNumOfR && (int) (holdLitSize - vNewR.size()) > StrategyParam::MaxLitNumOfR) {
    //        //pasLit->usedCount += StrategyParam::LIT_OVERLIMIT_WIGHT;
    //        ++Env::S_OverMaxLitLimit_Num;
    //        return ResRule::MoreLit;
    //    }


    uint8_t delRNum = 0; //删除的R的个数

    //若有变元替换发生则做如下检查：
    //4.对R文字检查 PS.若没有合一替换发生,则不需要检查[优化]
    //3.对主界线(A)文字检查 PS.若没有合一替换发生,则不需要检查[优化]

    if (isVarChg) {
        //----- 检查R中的文字,注意从后向前检查,相同R 删除后生成的R ------// 
        int iRIndA = vNewR.size() - 1;
        while (iRIndA >-1) {
            Lit_p rLitA = vNewR[iRIndA];
            int iRIndB = iRIndA - 1;
            while (iRIndB>-1) {
                Lit_p rLitB = vNewR[iRIndB];
                if (rLitA->equalsStuct(rLitB)) {
                    // 4.1 R中的文字互补                        [换被动文字]
                    if (rLitB->isComplementProps(rLitA)) {
                        return ResRule::ChgPasLit;
                    }
                    //4.2 R中的文字相同,删除后生成的R          [记录下来,统一删除].
                    assert(rLitB->isSameProps(rLitA));
                    ++delRNum;
                    arryDelRInd[iRIndA] = 1; //设置该文字被删除                    
                    break;
                }
                --iRIndB;
            }
            --iRIndA;
        }

        //----- 检查主界线(A)文字 ------// 

        int vRSize = vNewR.size();
        int iALitInd = vALitTri.size() - 1;
        while (iALitInd > 0) {
            Lit_p aLitPtr = vALitTri[iALitInd]->alit;

            //------ 3.1 主界线(A)文字,相互之间不能相同/互补(说明当前的替换导致互补或相同)--[ 换被动文字 ]
            if (StrategyParam::IS_ALitNoEqual) {
                int iALitIndB = iALitInd - 1;
                while (iALitIndB >-1) {
                    Lit_p aLitB = vALitTri[iALitIndB]->alit;
                    if (aLitPtr->equalsStuct(aLitB)) {
                        ++Env::S_ASame2A_Num;
                        //输出全局信息
                        FileOp::getInstance()->outGlobalInfo(" #A2A:" + to_string(Env::S_ASame2A_Num));
                        // FileOp::getInstance()->outLog("S_ASame2A_Num" + to_string(Env::S_ASame2A_Num));
                        return ResRule::ChgPasLit;
                    }
                    --iALitIndB;
                }
            }
            {
                iRIndA = 0;
                while (iRIndA < vRSize) {
                    if (vNewR[iRIndA]->claPtr == aLitPtr->claPtr) {
                        int i = iRIndA;
                        do {
                            Lit_p litr = vNewR[i];
                            if (aLitPtr->equalsStuct(litr)) {
                                //4.4 R中的文字与该子句中的主界线文字互补(恒真) [换被动文字].
                                if (aLitPtr->isComplementProps(litr)) {
                                    return ResRule::ChgPasLit;
                                } else {
                                    //4.3 R中的文字与该子句中的主动文字相同       [记录下来,统一删除].                                    
                                    arryDelRInd[i] = 1;
                                    ++delRNum;
                                    break;
                                }
                            }
                            i++;
                        } while (i < vRSize && vNewR[i]->claPtr == aLitPtr->claPtr);
                        vRSize = iRIndA;
                        break;
                    }
                    //3.2 与前面R中文字不能相同                [换被动文字]
                    if ((StrategyParam::IS_ALitNoEqual)&&(aLitPtr->isSameProps(vNewR[iRIndA]))) {
                        if (aLitPtr->equalsStuct(vNewR[iRIndA])) {
                            ++Env::S_ASame2R_Num;
                            //输出全局信息
                            FileOp::getInstance()->outGlobalInfo(" #A2R:" + to_string(Env::S_ASame2R_Num));
                            return ResRule::ChgPasLit;
                        }
                    }
                    ++iRIndA;
                }
            }
            --iALitInd;
        }
    }

    //亲,再加一个 剩余文字判断, come on来啊!相互伤害啊~~
    
    /*限制子句中文字数个数 剩余R+主动子句剩余文字数+候选子句文字数-2<=limit*/
    //    if (0 < StrategyParam::MaxLitNumOfR && (int) (holdLitSize - delRNum) > (int) StrategyParam::MaxLitNumOfR) {
    //        //pasLit->usedCount += StrategyParam::LIT_OVERLIMIT_WIGHT;
    //        ++Env::S_OverMaxLitLimit_Num;
    //        return ResRule::MoreLit;
    //    }

    //这个规则方法写的好痛苦^O^  终于到这里了=== 
    //1. 检查FS归入冗余 A. 将R中没有被删除的文字添加到holdLit数组中
    for (int i = 0; i < vNewR.size(); ++i) {
        if (arryDelRInd[i] == 0) {
            holdLit[holdLitSize++] = vNewR[i];
        }
    }


    //B. 检查posCal剩余子句+ vNewR 是否是冗余的(FS:向前归入冗余/恒真)                
    if (fol->holdLitsIsRundacy(holdLit, holdLitSize, setUsedCla, pasCla)) {
        //   fprintf(stdout, "检查:C%ud + C%ud 剩余文字发生冗余\n", actCla->ident, pasCla->ident);
        return ResRule::RSubsump; //子句冗余
    }
    //天啊~ 还不没完啊 ,快崩溃了~
    //开始标注主动子句中的被删除的剩余文字,被动子句中的被删除的剩余文字,删除R中的文字,添加主动子句中文字到R中.OMG!!!
    for (Literal* delLit : vDelLit) {
        delLit->EqnDelProp(EqnProp::EPIsHold);
    }
    candLit->EqnDelProp(EqnProp::EPIsHold);

    //删除R 从后往前删除
    for (int i = vNewR.size() - 1; i>-1; --i) {
        if (1 == arryDelRInd[i]) {
            vNewR.erase(vNewR.begin() + i);
        }
    }
    //添加主动子句中的剩余文字到R中
    for (int i = uPasClaHoldLitSize; i < uActClaHoldLitSize + uPasClaHoldLitSize; ++i) {
        assert(holdLit[i]->claPtr = actCla);
        vNewR.push_back(holdLit[i]);
    }
    return ResRule::RULEOK;
}

/**
 * 单元子句约减后的规则检查; 与上面规则检查不同在于,检查的规则简单了,而且只要没有替换发生则不需要调用
 * 注意:arrayHoldLits为传入的剩余文字集合,原本是可以不需要传入的,
 * 但是考虑到每次方法结束后都要对该数组进行释放,影响效率,因此放入调用方法中,只申请一次,释放一次
 * 最好放入栈中,主动释放;而不是堆上手动释放
 * @param actCla    传入的主动子句;(被单元子句约减的子句)
 * @param arrayHoldLits 传入的剩余文字集合,PS
 * @return 规则检查结果
 */
ResRule TriAlg::RuleCheckUnitReduct(Clause*actCla, Literal* *arrayHoldLits) {
    /*找到可以合一的单文字子句,同样需要检查是否满足一些规则问题，单元子句使得这些检查更简单。
     ** 分析：
     ** 1.主动文字检查:主动文字就是单文字,可能存在的情况;
     ** 1.1.单文字与主界线文字相同                                   [允许]
     **      分析:例如主界线文字P(a,b),actCla中的剩余文字~P(a,X),单文字子句P(X,b).合一后单文字为P(a,b),相当于是做了一次合一下拉。因此允许。
     ** 1.2.单文字与主界线文字互补                                   [不存在该情况]
     **      分析:可能互补的主界线文字,在优先查找互补单文字的时候,就已经下拉了.
     ** 1.3.单文字与R文字互补--早就下拉了                            [不存在该情况]
     ** 1.4.单文字与R文字相同                                        [允许]
     ## 算法结论:不检查主动子句
     * -----------------------------
     ** 2.被动文字检查:
     **  2.1.被动文字与主界线相同(同1.2)                             [不存在该情况]
     **  2.2 被动文字与主界线互补(同1.1)                             [允许]
     **  2.3 被动文字与R文字互补(同1.4)                              [允许]
     **  2.4 被动文字与R文字相同(同1.3)                              [不存在]
     ** 
     **  2.5 被动子句中剩余文字之间互补(不合一)                      [换单元子句]
     **  2.6 被动子句中剩余文字之间相同(不合一)                      [记录下来,统一给出删除属性]
     ** 
     **  2.7 被动子句中剩余文字与主界线互补                          [换单元子句]?? [记录下来,统一给出删除属性]
     **  2.8 被动子句中剩余文字与主界线相同                          [允许]    
     ** 
     **  2.9  被动子句中剩余文字与R中文字相同                        [记录下来,统一给出删除属性]
     **  2.10 被动子句中剩余文字与R中文字互补                        [换单元子句] PS:恒真是因为替换导致的,若原本恒真应该在预处理阶段解决
     ## 算法:遍历被动子句文字,检查相互之间关系, 遍历主界线文字关系,遍历R文字
     * -----------------------------
     ** 3.主界线文字检查
     ** 3.1.替换导致原有主界线文字相同                               [允许]
     ** 3.2.替换导致原有主界线文字互补(暂时不做判断,就当是一个冗余子句的参与) ---  后续优化的时候 可以考虑 
     **      分析: 例子 { C1; P1(a1,X2)+~P1(X1,a2)+P(X1,X2); C2: P1(X1,a2)+R ; C3:~P1(a1,X2)+Q  C4:P(a1,a2)
     ** 3.3 与前面的R文字相同                                        [换单元子句] PS:后面可以考虑允许这种情况发生,相当于主界线的文字没有发挥作用
     ** 3.4 与前面的R文字互补                                        [允许]    
     ## 算法结论:将这个检查放到R的遍历中                
     * -----------------------------
     ** 4.R文字检查
     ** 4.1 R文字之间互补                                            [换单元子句]
     ** 4.1 R文字之间相同                                            [[记录下来,统一删除]
     ** 
     ** 5.产生的剩余文字是否是冗余的。 
     */


    //有变元替换发生,做规则检查

    int16_t uHoldLitsNum = 0;
    int16_t delRNum = 0; //删除的R文字个数
    int16_t arryDelRInd[vNewR.size()]; //删除的R文字编号
    //memset(arryDelRInd, 0, sizeof (arryDelRInd));   //全部初始化为0; - 没有必要初始化为0,反正都要赋值覆盖的
    vector<Literal*>vDelActLit; //记录被删除的主动子句中的文字
    vDelActLit.reserve(2);
    //------- 遍历被动子句中文字 ------ 
    for (Literal* litPtrA = actCla->literals; litPtrA; litPtrA = litPtrA->next) {
        bool isDel = false;
        if (!litPtrA->EqnQueryProp(EqnProp::EPIsHold)) {
            continue;
        }
        //== 检查被动子句中剩余文字相互之间相等/互补
        for (Literal* litPtrB = litPtrA->next; litPtrB; litPtrB = litPtrB->next) {
            if (!litPtrB->EqnQueryProp(EqnProp::EPIsHold))
                continue;
            if (litPtrA->equalsStuct(litPtrB)) {
                //2.5 被动子句中剩余文字之间互补                [换单元子句]
                if (litPtrA->isComplementProps(litPtrB)) {
                    return ResRule::ChgActLit;
                }//2.6 被动子句中剩余文字之间相同               [记录下来,统一给出删除属性]
                else {
                    isDel = true;
                    vDelActLit.push_back(litPtrA);
                    break;
                }
            }
        }
        if (isDel)
            continue;

        //== 检查主界线文字与被动子句中的剩余文字互补        
        for (ALit_p litTri : vALitTri) {
            Literal* alit = litTri->alit;
            if (litPtrA->isComplementProps(alit) && litPtrA->equalsStuct(alit)) {
                //2.7 被动子句中剩余文字与主界线互补                          [换单元子句]X -- 应该改为下拉
                //return ResRule::ChgActLit;
                isDel = true;
                vDelActLit.push_back(litPtrA);
                FileOp::getInstance()->outLog(to_string(Env::global_clause_counter) + "?? 出现有疑问状态-->单元子句合一下拉后,剩余文字与主界线文字互补!请做最终确定\n");
                break;
            }
        }
        if (isDel)
            continue;
        //==检查R中文字与被动子句中剩余文字互补/相同
        for (Literal* litr : vNewR) {
            if (litPtrA->equalsStuct(litr)) {
                //2.10 被动子句中剩余文字与R中文字互补                        [换单元子句]
                if (litPtrA->isComplementProps(litr)) {
                    return ResRule::ChgActLit;
                } else {
                    //2.9 被动子句中剩余文字与R中文字相同                        [记录下来,统一给出删除属性]
                    isDel = true;
                    vDelActLit.push_back(litPtrA);
                    break;
                }
            }
        }
        if (!isDel) {
            arrayHoldLits[uHoldLitsNum++] = litPtrA; //添加到剩余文字列表中
        }
    }

    //------ 检查R中的文字,注意从后向前检查,相同R 删除后生成的R ------// 
    int16_t iRIndA = vNewR.size() - 1;
    for (; iRIndA >-1; --iRIndA) {
        bool isdel = false;
        Lit_p rLitA = vNewR[iRIndA];
        //== 检查R文字与后续的主界线文字相同
        int iTriLitInd = vALitTri.size() - 1;
        while (iTriLitInd>-1) {
            Literal* alit = vALitTri[iTriLitInd]->alit;
            if (alit->claPtr == rLitA->claPtr) {
                //说明剩余文字与主界线文字目前在同一个子句中,检查剩余文字是否与主界线文字相同(合并删除)
                if (rLitA->isSameProps(alit) && alit->equalsStuct(rLitA)) {
                    arryDelRInd[delRNum++] = iRIndA; //记录被删除的R文字                    
                    isdel = true;
                }
                break;
            }
            //3.3 与前面的R文字相同(即:R文字与后面的主界线文字不能相同)                [换单元子句] 
            if (rLitA->isSameProps(alit) && alit->equalsStuct(rLitA))
                return ResRule::ChgPasLit;
            --iTriLitInd;
        }
        if (isdel)continue;
        for (int iRIndB = iRIndA - 1; iRIndB>-1; --iRIndB) {
            Lit_p rLitB = vNewR[iRIndB];
            if (rLitA->equalsStuct(rLitB)) {
                // 4.1 R文字之间互补                                            [换单元子句]
                if (rLitB->isComplementProps(rLitA)) {
                    return ResRule::ChgPasLit;
                }
                //4.2 R文字之间相同                                             [记录下来,统一删除]
                assert(rLitB->isSameProps(rLitA));
                arryDelRInd[delRNum++] = iRIndA; //记录被删除的R文字  
                isdel = true;
                break;
            }
        }
        if (!isdel) {
            arrayHoldLits[uHoldLitsNum++] = rLitA;
        }
    }

    //------ 检查剩余文字 是否是冗余的(FS:向前归入冗余/恒真)                
    if (fol->holdLitsIsRundacy(arrayHoldLits, uHoldLitsNum, setUsedCla, nullptr)) {
        return ResRule::RSubsump; //子句冗余
    }
    //开始标注主动子句中的被删除的剩余文字,被动子句中的被删除的剩余文字,删除R中的文字,添加主动子句中文字到R中.OMG!!!
    for (Literal* delLit : vDelActLit) {
        delLit->EqnDelProp(EqnProp::EPIsHold);
    }

    //删除R 从后往前删除
    for (int i = 0; i < delRNum; ++i) {
        vNewR.erase(vNewR.begin() + arryDelRInd[i]);
    }
    return ResRule::RULEOK;

}

ResRule TriAlg::RuleCheckLastHope(Literal * actLit) {
    /*规则检查 ，
     ** 总的原则:  主界线上文字不能相同或合一互补; 剩余文字不能相同或恒真; 
     ** PS:纯粹一些只做 规则检查不删除(合并)任何文字 
      
     ** 1.主界线(A)文字与前面剩余(B)文字不能相同 [不做合一].
     ** 1.1.检查主动文字与前面剩余文字相同 [ 直接换主动文字 ]
     ** 1.2.其它主界线文字与前面B文字相同 [ 说明由于替换导致相同,换下一个被动归结文字 ]
     ** 1.3.主动文字与前面主界线文字相同 [直接换主动文字]
    
     ** 2.对被动归结子句中剩余文字
     ** 2.1.检查与对角线文字互补 [删除(下拉)] -- * 注意需要是否合一问题,优先做检查 *       
     ** 2.2.检查被动文字与对角线文字相同[ 换下一个主动文字 ] -- 不允许主界线文字相同(不考虑合一)
     ** 
     ** 3.检查剩余文字
     ** 3.1.剩余文字之间是否恒真； [ 换下一个被动归结文字 ]
     ** 3.2.检查与前面剩余文字恒真 [ 换下一个被动归结文字 ]
     ** 3.3.检查与前面剩余文字相同 [ 删除(合并) ]
     */

    /// 剩余文字与主动文字互补情况，放入加入剩余文字集合R中的时候进行检查？

    vector<int>vDelLit; //被合并的文字--包括 1.已有剩余文字 相同合并. 2.当前被动文字 与 主动文字(或主界线文字)互补合并[直接下拉了]
    vDelLit.reserve(8);
    int iALitBInd = 0;
    int vRSize = vNewR.size();
    int iRInd = vRSize - 1;

    //==== 遍历已有的剩余文字(B文字) 确保主动文字不能与剩余(B)文字相同,剩余(B)文字R之间不能互补(不做合一替换)=====================//   
    while (iRInd >-1) {

        Lit_p rLitA = vNewR[iRInd];
        //1.1.主动文字与前面剩余文字相同 [ 直接换主动文字 ] -- 有可能剩余文字合并
        if (actLit->isSameProps(rLitA) && actLit->equalsStuct(rLitA)) {
            return ResRule::ChgActLit;
        }

        int iRIndB = iRInd - 1;
        //3. 检查剩余文字之间 是否存在恒真/相等情况
        while (iRIndB>-1) {
            Lit_p rLitB = vNewR[iRIndB];
            if (rLitA->equalsStuct(rLitB)) {
                //3.1.剩余文字之间恒真； [ 换下一个被动归结文字 ]
                if (rLitB->isComplementProps(rLitA)) {
                    return ResRule::ChgPasLit;
                }
                //3.2 检查与前面剩余文字相同 [ 记录删除(合并) ]
                assert(rLitB->isSameProps(rLitA));
                vDelLit.push_back(iRInd);
                break;
            }
            --iRIndB;
        }
        --iRInd;
    }
    iRInd = 0;
    int iALitInd = vALitTri.size() - 1;
    //==== 遍历主界线(A文字) 确保主界线文字不能互补(不做合一替换) =====================//   
    while (iALitInd > 0) {
        Lit_p aLitPtr = vALitTri[iALitInd]->alit;
        //------1.1检查当前主动文字与主界线文字是否相同/是否互补 ------ 
        if (actLit->equalsStuct(aLitPtr)) {
            if (actLit->isComplementProps(aLitPtr)) //互补情况[ 换下一个主动归结文字 ]
                return ResRule::ChgActLit;
            assert(actLit->isSameProps(aLitPtr)); //相同情况[ 换下一个被动归结文字 ]
            return ResRule::ChgPasLit;
        }
        iALitBInd = iALitInd - 1;
        //------ 1.2 检查主界线文字之间 是否相同/互补(说明当前的替换导致互补或相同)--[ 换下一个被动归结文字 ]
        while (iALitBInd >-1) {
            Lit_p aLitB = vALitTri[iALitBInd]->alit;
            if (aLitPtr->equalsStuct(aLitB)) {
                return ResRule::ChgPasLit;
            }
            --iALitBInd;
        }
        //------ 1.3 检查主界线与前面剩余文字相同 -- [ 换下一个被动归结文字 ]        
        while (iRInd < vRSize) {
            if (vNewR[iRInd]->claPtr == aLitPtr->claPtr) {
                vRSize = iRInd + 1;
                break;
            }
            if (aLitPtr->isSameProps(vNewR[iRInd])) {
                if (aLitPtr->equalsStuct(vNewR[iRInd])) {
                    return ResRule::ChgPasLit;
                }
            }
            ++iRInd;
        }
        --iALitInd;
    }
    //====== 处理应该被合并的文字 1.已有剩余文字R,合并相同. 2.当前被动子句中剩余文字与主动文字(或主界线文字)互补合并(不做合一替换) ====//
    /*1.如果检查通过,将剩余文字vNewR中相同的文字删除 */
    for (int i = 0; i < vDelLit.size(); ++i) {
        vNewR.erase(vNewR.begin() + vDelLit[i]);
    }
    return ResRule::RULEOK;
}

/*
 ** 生成子句后,最后使用主界线对R 进行合一下拉操作
 ** 算法:从最后一个R文字开始 检查是否可以合一下拉
 ** 返回删除R文字数. 并修改vNewR中的文字
 */
int TriAlg::TriMguReduct() {

    int rollbackPos = 0;
    int delNum = 0;
    for (int i = vNewR.size() - 1; i>-1; --i) {
        Literal* litR = vNewR[i];
        for (ALit_p aTri : vALitTri) {
            Lit_p litTri = aTri->alit;
            if (litTri->claPtr == litR->claPtr)
                break;
            if (litR->isComplementProps(litTri)) {
                rollbackPos = subst->Size();
                if (unify.literalMgu(litR, litTri, subst)) {
                    //下拉文字
                    if ((i + 1) != vNewR.size())
                        vNewR[i] = vNewR.back();
                    vNewR.pop_back();
                    ++delNum;
                    break;
                }
                subst->SubstBacktrackToPos(rollbackPos);
            }
        }
    }
    return delNum;
}

/*-----------------------------------------------------------------------
 * == 处理主动归结子句,并将剩余文字添加子句到newR 中 
 * 1.对newR进行下拉操作 注意相同合并操作已经在rule检查中完成；
 *  1.1。检查是否与剩余文字所在子句之前的主界线文字互补 [ 下拉操作 ]
 * 2.对主动子句剩余文字
 *  2.1，检查是否与归结文字互补（恒真情况）-- 保守点    [ 换下一个被动归结文字 ]
 *  2.2. 检查是否与归结文字相同                         [ 合并删除该剩余文字 ]
 *  2.3. 检查是否与主界线文字互补                       [ 下拉操作 ]
 * 
/*---------------------------------------------------------------------*/

/**
 *  这种情况是 actLit 找到了posLIt 继续拓展了三角形的情况
 * @param actLit  
 * @return ResRule::ChgPasLit;  ResRule::RULEOK;
 */
ResRule TriAlg::actClaProcAddNewR(Lit_p actLit) {
    //== 1.对newR进行下拉操作 注意A，只检查剩余文字所在子句之前的主界线，B相同合并操作已经在rule检查中完成；
    size_t newRSize = vNewR.size();
    vector<uint16_t> vDelLit;
    vDelLit.reserve(newRSize);
    Lit_p litR = nullptr;
    for (uint16_t i = 0; i < newRSize; ++i) {
        litR = vNewR[i];
        for (ALit_p triLit : vALitTri) {
            if (triLit->alit->claPtr == litR->claPtr) {
                if (triLit->alit->equalsStuct(litR)) {
                    if (triLit->alit->isComplementProps(litR)) {//互补(恒真）
                        return ResRule::ChgPasLit;
                    }
                    //相同则合并删除
                    vDelLit.push_back(i);
                }
                break;
            }
            //2.3. 检查是否与主界线文字互补                       [ 下拉操作 ] 
            if (triLit->alit->equalsStuct(litR) && triLit->alit->isComplementProps(litR)) {
                ++triLit->reduceNum; //记录下拉次数
                vDelLit.push_back(i);
            }
        }
    }
    //== 2.对主动子句中的剩余文字添加到newR中
    Lit_p litPtr = actLit->claPtr->literals;
    int addNum = 0;
    while (litPtr) {

        if (litPtr == actLit) {
            litPtr->EqnDelProp(EqnProp::EPIsHold);
            litPtr = litPtr->next;
            continue;
        }
        bool isHold = litPtr->EqnQueryProp(EqnProp::EPIsHold);
        if (isHold) {
            if (actLit->equalsStuct(litPtr)) {
                //2.1.与主动文字互补 [换下一个被动归结文字]；--同一子句两个文字互补 恒真
                if (actLit->isComplementProps(litPtr)) {
                    assert(actLit->claPtr == litPtr->claPtr);
                    //还原vNewR
                    for (int i = 0; i < addNum; i++) {
                        vNewR.pop_back();
                    }
                    return ResRule::ChgPasLit;
                }
                // 2.2.与主动文字相同 [合并删除]；
                assert(actLit->isSameProps(litPtr));
                litPtr->EqnDelProp(EqnProp::EPIsHold);
                litPtr = litPtr->next;
                continue;
            }
            assert(isHold);
            for (ALit_p triLit : vALitTri) {
                Lit_p tmpLit = triLit->alit;
                //2.3.与主界线剩余文字相同 [下拉]
                if (litPtr->isComplementProps(tmpLit) && litPtr->equalsStuct(tmpLit)) {
                    litPtr->EqnDelProp(EqnProp::EPIsHold);
                    ++triLit->reduceNum; //记录下拉次数
                    isHold = false;
                    break;
                }
            }
        }
        if (isHold) {
            vNewR.push_back(litPtr);
            ++addNum;
        }
        litPtr = litPtr->next;
    }
    /*如果检查通过,将vNewR中约减的文字删除 */
    for (int i = vDelLit.size() - 1; i >-1; --i) {
        vNewR.erase(vNewR.begin() + vDelLit[i]);
    }
    vector<uint16_t>().swap(vDelLit);
    return ResRule::RULEOK;
}

/*-----------------------------------------------------------------------
 ** 对被动归结子句进行处理
 ** 1.对newR进行下拉操作 注意相同合并操作已经在rule检查中完成；
 **  1.1。检查是否与剩余文字所在子句之前的主界线文字互补 [ 下拉操作 ]
 ** 1。对被动归结子句进行约减处理；
 **  1.1.检查是否与被归结文字互补（恒真情况）-- 保守点      [ 换下一个被动归结文字 ]
 **  1.2.检查是否与被动归结文字相同；                       [ 合并删除该剩余文字 ]
 **  1.3 检查是否与vNewR文字 互补 -- 恒真                   [ 换下一个被动归结文字 ]
 **  1.4 检查是否与vNewR文字 相同                           [ 合并删除该剩余文字 ]
 **  1.5.检查是否与主界线文字互补                           [ 下拉操作 ] * 

 ** 2。检查posCal剩余子句+ vNewR 是否是冗余的
------------------------------------------------------------------------*/
//

ResRule TriAlg::pasClaProc(Lit_p candLit, uint16_t & uPasHoldLitNum) {

    Lit_p bLit = candLit->claPtr->Lits();
    uPasHoldLitNum = 0;
    //====== 1。对被动归结子句进行约减处理；
    for (; bLit; bLit = bLit->next) {
        bLit->EqnDelProp(EqnProp::EPIsHold);
        if (bLit == candLit) {
            continue;
        }
        /*检查被动选择文字*/
        if (bLit->equalsStuct(candLit)) {
            //1.1. 检查是否与被归结文字互补（恒真情况）-- 保守点      [ 换下一个被动归结文字 ]
            if (bLit->isComplementProps(candLit)) {
                return ResRule::ChgPasLit;
            }
            //1.2. 检查是否与被动归结文字相同；                       [ 合并删除该剩余文字 ]
            continue;
        }
        bool isHold = true;
        /*检查剩余文字R*/
        for (Lit_p litR : vNewR) {
            if (bLit->equalsStuct(litR)) {
                //1.3 检查是否与vNewR文字 互补 -- 恒真                   [ 换下一个被动归结文字 ]
                if (bLit->isComplementProps(litR)) {
                    return ResRule::ChgPasLit;
                }
                //1.4 检查是否与vNewR文字 相同                           [ 合并删除该剩余文字 ]
                isHold = false;
                break;
            }
        }
        //1.5. 检查是否与主界线文字互补                               [ 下拉操作 ]
        if (isHold) {
            for (ALit_p aLitE : vALitTri) {
                if (bLit->isComplementProps(aLitE->alit) && bLit->equalsStuct(aLitE->alit)) {
                    ++aLitE->reduceNum;
                    isHold = false;
                    break;
                }
            }
        }
        if (isHold) {
            ++uPasHoldLitNum;
            bLit->EqnSetProp(EqnProp::EPIsHold);
        }
    }
    //2. 检查posCal剩余子句+ vNewR 是否是冗余的(FS:向前归入冗余/恒真)                
    if (fol->leftLitsIsRundacy(candLit->claPtr->Lits(), uPasHoldLitNum, vNewR, setUsedCla)) {
        return ResRule::RSubsump; //子句冗余
    }
    return ResRule::RULEOK;

}

//生成新子句

Clause * TriAlg::getNewCluase(Clause * pasCla) {
    Clause* newCla = new Clause();
    vector<Literal*>vTmpR(vNewR);
    Lit_p pasLitptr = pasCla ? pasCla->literals : nullptr;
    for (; pasLitptr; pasLitptr = pasLitptr->next) {
        if (pasLitptr->EqnQueryProp(EqnProp::EPIsHold)) {
            vTmpR.push_back(pasLitptr);
        }
    }
    newCla->bindingAndRecopyLits(vTmpR);
    return newCla;
}

void TriAlg::ClearResVTBinding() {
    subst->Clear();
}

bool TriAlg::unitResolutionBySet(Literal* gLit, int ind) {

    vector<Clause* >&cmpUnitClas = gLit->IsPositive() ? fol->vNegUnitClas : fol->vPosUnitClas;

    for (; ind < cmpUnitClas.size(); ++ind) {
        Clause* candUnitCal = cmpUnitClas[ind];
        if (setUsedCla.find(candUnitCal) != setUsedCla.end()) {
            continue;
        }

        Lit_p candLit = candUnitCal->Lits();
        assert(gLit->isComplementProps(candLit));
        int backpoint = subst->Size();
        bool res = unify.literalMgu(gLit, candLit, subst);

        if (res) {
            //找到可以合一的单文字子句,1.添加单元子句文字到A文字列表中;2.添加到被使用文字中
            setUsedCla.insert(candUnitCal);
            vALitTri.push_back(new ALit{0, -1, candLit, gLit});
            //输出该对角线文字-------------------------
            {
                //                fprintf(stdout, "单元子句下拉-[C%u_%d]", candLit->claPtr->ident, candLit->pos);
                //                candLit->EqnTSTPPrint(stdout, true);
                //                cout << endl;
                //
                //                fprintf(stdout, "[C%u_%d]", gLit->claPtr->ident, gLit->pos);
                //                gLit->EqnTSTPPrint(stdout, true);
                //                cout << endl;
            }
            //------------------------------------------

            return true;
        }
        subst->SubstBacktrackToPos(backpoint);
    }

    return false;
}
/// 
/// \param gLit
/*-----------------------------------------------------------------------
 * 给定一个子句,检查是否可以用单元子句进行约减
/*---------------------------------------------------------------------*/
//

bool TriAlg::unitResolutionrReduct(Lit_p *actLit, uint16_t & uActHoldLitNum) {
    Clause* claPtr = (*actLit)->claPtr;

    Lit_p retLitPtr = nullptr;
    bool isReduct = false;
    set<Clause*>cmpCla;

    //只声明一次,一个剩余文字列表,避免重复申请释放空间.PS:要使用这个数组,至少有一个文字被下拉所以-1
    Literal * arrayHoldLits[uActHoldLitNum + vNewR.size() - 1];

    Lit_p givenLitP = claPtr->literals;

    //---- 遍历检查子句的所有剩余文字, 检查所有单元子句下拉情况 ------
    for (; givenLitP; givenLitP = givenLitP->next) {
        if (!givenLitP->EqnQueryProp(EqnProp::EPIsHold)) {
            continue;
        }

        //bool flag = false;
        // 约束条件:同一个单元子句 [更名后]，不能对同一个子句中的文字进行下拉. 
        /*算法处理: 1.找到一个可以匹配的 单元子句,则将该单元子句移动到列表最后(降低使用频率)
         * 2.当前的后续检查将不再使用该文字进行匹配操作    
         */
        vector<Clause* >&cmpUnitClas = givenLitP->IsPositive() ? fol->vNegUnitClas : fol->vPosUnitClas; //可以考虑优化 用索引树

        //------ 遍历单元子句 ------
        for (int ind = 0; ind < cmpUnitClas.size(); ++ind) {
            Clause* candUnitCal = cmpUnitClas[ind];
            if (candUnitCal->isDel()) { //被删除的单元子句 不处理               
                continue;
            }

            Lit_p candLit = candUnitCal->Lits();
            //若GivenLitP不是等词，则确保lterm.fcode相等（优化）
            if (!givenLitP->EqnIsEquLit() && givenLitP->lterm->fCode != candLit->lterm->fCode)
                continue;

            //注意:当前单元子句没有经过任何替换,因此判断单元子句是否为基项,不需要重新计算
            bool isRN = !candLit->IsGround(false);

            //单元子句重用:对非基文字拷贝生成新的单元子句，放入临时单元子句列表,不加入子句集. 完成三角形后 该临时单元子句被删除.
            if (isRN) {
                candUnitCal = new Clause();
                candLit = candLit->RenameCopy(candUnitCal, DerefType::DEREF_NEVER);
            }
            assert(givenLitP->isComplementProps(candLit));

            int backpoint = subst->Size();
            bool res = unify.literalMgu(givenLitP, candLit, subst);
            //=== 合一成功 ===
            if (res) {
                givenLitP->EqnDelProp(EqnProp::EPIsHold);
                //===1.if 起步子句,else if 2有变元替换发生,做规则检查 
                if (vALitTri.empty() || backpoint != subst->Size()) {
                    // cout<<"是否发生逆向替换:"<<subst->isReverseSubst(claPtr->ident,backpoint)<<endl;
                    ResRule res = RuleCheckUnitReduct(claPtr, arrayHoldLits);
                    if (ResRule::RULEOK != res) {//剩余文字冗余的(FS:向前归入冗余/恒真)    

                        //debug         cout << "检查posCal剩余子句: ";                        claPtr->ClauseTSTPPrint(stdout, true, false);                        cout << endl;

                        if (isRN) {
                            DelPtr(candLit); //删除子句
                            DelPtr(candUnitCal);
                            --Env::global_clause_counter;
                        }
                        if (ResRule::RSubsump == res) {
                            //res说明该单元文字与该子句进行下拉 容易造成冗余,是否可以修改权重,
                            givenLitP->usedCount += StrategyParam::LIT_REDUNDANCY_WIGHT;
                            claPtr->priority -= StrategyParam::CLA_REDUNDANCY_WIGHT;
                        }
                        subst->SubstBacktrackToPos(backpoint);
                        givenLitP->EqnSetProp(EqnProp::EPIsHold);
                        continue; //冗余当前单文字子句不合适 继续尝试下一个单文字子句  
                    }
                }
                //=== 找到约减的单元子句,后续处理
                {
                    isReduct = true;
                    givenLitP->EqnDelProp(EqnProp::EPIsHold);
                    if (isRN) {
                        candUnitCal->bindingLits(candLit);
                        //输出新子句到info文件
                        outNewClaInfo(candUnitCal, InfereType::RN);
                        //输出到.r文件
                        string str = "\n";
                        cmpUnitClas[ind]->literals->getLitInfo(str);
                        cmpUnitClas[ind]->literals->getStrOfEqnTSTP(str);
                        str += "\nR[" + to_string(candUnitCal->ident) + "]:";
                        candLit->getParentLitInfo(str);
                        candLit->getStrOfEqnTSTP(str);
                        FileOp::getInstance()->outRun(str);
                        //添加该单元子句副本到删除列表中,完成三角形后删除.
                        delUnitCla.push_back(candUnitCal);
                    }
                    setUsedCla.insert(candUnitCal);
                    ++givenLitP->usedCount; //记录主动文字使用次数
                    //添加到主界线
                    vALitTri.push_back(new ALit{0, -1, candUnitCal->literals, givenLitP});
                    //输出单元子句变名
                    //                    if (isRN) {
                    //                        string litInfo = "";
                    //                        fprintf(stdout, "%s", candLit->getLitInfo(litInfo));
                    //                        candLit->EqnTSTPPrint(stdout, true, DerefType::DEREF_NEVER);
                    //                        fprintf(stdout, "\nR[%u]:[C%u_%u]", candUnitCal->ident, candLit->parentLitPtr->claPtr->ident, candLit->parentLitPtr->pos);
                    //                        candUnitCal->literals->EqnTSTPPrint(stdout, true);
                    //                        fprintf(stdout, "\n");
                    //                       flag = true;
                    //                        candUnitCal->ClausePrint(stdout, true);
                    //                        fprintf(stdout, "\n");
                    //                    }
                }

                Clause* tmpCla = cmpUnitClas[ind]; //*(vec.end() - 1);
                //记录该文字使用次数

                //单元子句使用次数-1, 降低优先级
                tmpCla->priority -= 1;
                //移动匹配成功的 单元子句 -- {两种算法:A.没用一次移到最后,下次使用不用排序;B.每次使用记录weight,下次使用排序.目前使用A算法)
                cmpUnitClas.erase(cmpUnitClas.begin() + ind);
                cmpUnitClas.push_back(tmpCla);
                --uActHoldLitNum; //被约减子句中剩余文字数-1
                break; //匹配成功换下一个剩余文字
            } else {
                if (isRN) {
                    DelPtr(candLit); //删除子句
                    DelPtr(candUnitCal);
                    --Env::global_clause_counter;
                }
                subst->SubstBacktrackToPos(backpoint);
            }
        }

        if (retLitPtr == nullptr&&(givenLitP->EqnQueryProp(EqnProp::EPIsHold))) {
            retLitPtr = givenLitP;
        }
    }

    *actLit = retLitPtr;
    cmpCla.clear();
    return isReduct;
}


// <editor-fold defaultstate="collapsed" desc="输出相关">
//三角形结果输出

void TriAlg::printTri(FILE * out) {
    //输出主界线文字
    for (ALit_p elem : vALitTri) {

        Lit_p actLit = elem->alit;
        fprintf(out, "\n[C%u_%d]", actLit->claPtr->ident, actLit->pos);
        actLit->EqnTSTPPrint(stdout, true);
        Lit_p pasLit = elem->blit;

        fprintf(out, "\n[C%u_%d]", pasLit->claPtr->ident, pasLit->pos);
        pasLit->EqnTSTPPrint(out, true);

    }
    fprintf(out, "\n");
}

void TriAlg::outTri() {
    string outStr = "\n";
    for (ALit_p elem : vALitTri) {

        Lit_p actLit = elem->alit;
        actLit->getLitInfo(outStr);
        actLit->getStrOfEqnTSTP(outStr);
        outStr += "\n";

        Lit_p pasLit = elem->blit;
        pasLit->getLitInfo(outStr);
        pasLit->getStrOfEqnTSTP(outStr);
        outStr += "\n";
    }
    FileOp::getInstance()->outRun(outStr);

}

void TriAlg::outR(Literal * lit) {
    string outStr = "";
    size_t uSizeR = vNewR.size();
    if (0 == uSizeR && lit == nullptr) {
        outStr = "R:空子句";
        FileOp::getInstance()->outRun(outStr);
        //string cnf(c_0_6,plain,    ( $false ),    inference(sr,[status(thm)],[c_0_4,c_0_5]),    [proof]).
        return;
    }
    //输出R   

    outStr += "R[";
    outStr += (nullptr == lit) ? to_string(Env::global_clause_counter) + "]" : to_string(Env::global_clause_counter + 1) + "]";
    Lit_p tmpLitptr = lit;
    if (uSizeR > 0) {
        vNewR[0]->getLitInfo(outStr);
        vNewR[0]->getStrOfEqnTSTP(outStr);

        for (int i = 1; i < uSizeR; ++i) {
            outStr += "+";
            vNewR[i]->getLitInfo(outStr);
            vNewR[i]->getStrOfEqnTSTP(outStr);
        }

        for (; tmpLitptr; tmpLitptr = tmpLitptr->next) {
            if (tmpLitptr->EqnQueryProp(EqnProp::EPIsHold)) {
                outStr += "+";
                tmpLitptr->getLitInfo(outStr);
                tmpLitptr->getStrOfEqnTSTP(outStr);
            }
        }
    } else {
        assert(tmpLitptr);
        bool isAddPlus = false;
        while (tmpLitptr) {
            if (tmpLitptr->EqnQueryProp(EqnProp::EPIsHold)) {
                if (isAddPlus) {
                    isAddPlus = false;
                } else {

                    outStr += "+";
                }
                tmpLitptr->getLitInfo(outStr);
                tmpLitptr->getStrOfEqnTSTP(outStr);
            }
            tmpLitptr = tmpLitptr->next;
        }

    }
    outStr += "\n";
    FileOp::getInstance()->outRun(outStr);
}

void TriAlg::printR(FILE* out, Literal * lit) {

    size_t uSizeR = vNewR.size();
    if (0 == uSizeR && lit == nullptr)
        return;
    //输出R       
    fprintf(out, "R[%u]", (Env::global_clause_counter + 1));
    string litInfo = "";
    Lit_p tmpLitptr = lit;
    if (uSizeR > 0) {
        fprintf(stdout, "%s", vNewR[0]->getLitInfo(litInfo));
        vNewR[0]->EqnTSTPPrint(stdout, true);
        for (int i = 1; i < uSizeR; ++i) {
            litInfo = "";
            fprintf(stdout, " + %s", vNewR[i]->getLitInfo(litInfo));
            vNewR[i]->EqnTSTPPrint(stdout, true);
        }

        for (; tmpLitptr; tmpLitptr = tmpLitptr->next) {
            if (tmpLitptr->EqnQueryProp(EqnProp::EPIsHold)) {
                litInfo = "";
                fprintf(stdout, " + %s", tmpLitptr->getLitInfo(litInfo));
                tmpLitptr->EqnTSTPPrint(stdout, true);
            }
        }
    } else {
        bool isfirst = true;
        for (; tmpLitptr; tmpLitptr = tmpLitptr->next) {
            if (tmpLitptr->EqnQueryProp(EqnProp::EPIsHold)) {
                litInfo = "";
                if (isfirst) {
                    fprintf(stdout, "%s", tmpLitptr->getLitInfo(litInfo));
                    isfirst = false;
                } else {

                    fprintf(stdout, " + %s", tmpLitptr->getLitInfo(litInfo));
                }
                tmpLitptr->EqnTSTPPrint(stdout, true);
            }
        }
    }
    fprintf(out, "\n");
}

void TriAlg::outNewClaInfo(Clause* newCla, InfereType infereType) {

    string strCla = "";
    if (newCla == nullptr)
    {
        strCla = "cnf(c"+to_string(Env::global_clause_counter+1)+",plain,($false)";
        //    [c_0_4,c_0_5]),    [proof]).       
    }
    else
        newCla->getStrOfClause(strCla, false);

    if (this->setUsedCla.empty()) {
        strCla += ", 'proof' ).\n";
    } else {
        string parentCla = "";
        if (infereType == InfereType::RN) {
            parentCla = "c" + to_string(newCla->literals->parentLitPtr->claPtr->ident);
        } else {
            parentCla = "c" + to_string((*(setUsedCla.begin()))->ident);
            for_each(++setUsedCla.begin(), setUsedCla.end(), [&parentCla](Clause * cla) {
                parentCla += ",c" + to_string(cla->ident);
            });
        }
        strCla += ",inference(" + InferenceInfo::getStrInfoType(infereType) + ",[status(thm)],[" + parentCla + "]) ).\n";
    }
    FileOp::getInstance()->outInfo(strCla);
}
// </editor-fold>

