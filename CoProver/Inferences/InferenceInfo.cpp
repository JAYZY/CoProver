/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   InferenceInfo.cpp
 * Author: Zhong Jian<77367632@qq.com>
 * 
 * Created on 2019年3月21日, 上午10:17
 */

#include "InferenceInfo.h"
string InferenceInfo::TOInferTypeNames[] = {
    "RN", //rename
    "SCS"//矛盾体分离
};

InferenceInfo::InferenceInfo() {
}

InferenceInfo::InferenceInfo(const InferenceInfo& orig) {
}

InferenceInfo::~InferenceInfo() {
}

