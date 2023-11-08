#pragma once

#include "secure-join/Defines.h"

namespace secJoin{

    enum class ArrGateType : oc::u8
    {
        EQUALS = 1,
        NOT_EQUALS = 2,
        AND = 3,
        OR = 4,
        ADDITION = 5,
        LESS_THAN = 6,
        GREATER_THAN = 7
    };

    inline ArrGateType numToGateType(oc::u64 type)
    {
        if (type == 1)
            return ArrGateType::EQUALS;
        else if (type == 2)
            return ArrGateType::NOT_EQUALS;
        else if (type == 3)
            return ArrGateType::AND;
        else if (type == 4)
            return ArrGateType::OR;
        else if (type == 5)
            return ArrGateType::ADDITION;
        else if (type == 6)
            return ArrGateType::LESS_THAN;
        else if (type == 7)
            return ArrGateType::GREATER_THAN;
        
        std::string temp = "Gate Type not available for num = " + std::to_string(type)
             + "\n" + LOCATION;
        throw std::runtime_error(temp);
    }

    inline std::string gateToString(ArrGateType type)
    {
        if(type == 	   ArrGateType::EQUALS  )return "Equals";
		if(type == 	   ArrGateType::NOT_EQUALS   )return "Not Equals";
		if(type == 	   ArrGateType::AND)return "And";
		if(type == 	   ArrGateType::OR    )return "Or";
		if(type == 	   ArrGateType::ADDITION)return "Addition";
		if(type == 	   ArrGateType::LESS_THAN    )return "Less Than";
		if(type == 	   ArrGateType::GREATER_THAN   )return "Greater Than";
        return "";
    }

    struct ArrGate
	{
        ArrGateType mType;
        std::array<oc::u64, 2> mInput;
        oc::u64 mOutput;

        ArrGate(oc::u64 op, oc::u64 input1, oc::u64 input2, oc::u64 output)
        {
            mType = numToGateType(op);
            mInput[0] = input1;
            mInput[1] = input2;
            mOutput = output;
        }

        ArrGate()=default;

    };


    inline std::ostream& operator<<(std::ostream& o, ArrGate& gate)
    {
        o << gateToString(gate.mType) << " " 
            << gate.mInput[0] << " "
            << gate.mInput[1] << " "
            << gate.mOutput << std::endl;
        return o;
    }


}