#include <csnode/eventreport.hpp>
#include <csnode/datastream.hpp>
#include <csnode/node.hpp>

#include <lib/system/logger.hpp>

const char* log_prefix = "Event: ";

/*static*/
std::string Reject::to_string(Reason r) {
    switch (r) {
    case WrongSignature:
        return "WrongSignature";
    case InsufficientMaxFee:
        return "InsufficientMaxFee";
    case NegativeResult:
        return "NegativeResult";
    case SourceIsTarget:
        return "SourceIsTarget";
    case DisabledInnerID:
        return "DisabledInnerID";
    case DuplicatedInnerID:
        return "DuplicatedInnerID";
    case MalformedContractAddress:
        return "MalformedContractAddress";
    case MalformedTransaction:
        return "MalformedTransaction";
    case ContractClosed:
        return "ContractClosed";
    case NewStateOutOfFee:
        return "NewStateOutOfFee";
    case EmittedOutOfFee:
        return "EmittedOutOfFee";
    case CompleteReject:
        return "CompleteReject";
    default:
        break;
    }
    return "?";
}

/*static*/
void EventReport::sendReject(Node& node, const cs::Bytes& rejected) {
    std::map<Reject::Reason, uint16_t> resume;
    for (const auto r : rejected) {
        if (r != Reject::Reason::None) {
            resume[Reject::Reason(r)] += 1;
        }
    }
    if (!resume.empty()) {
        cs::Bytes bin_pack;
        cs::DataStream stream(bin_pack);
        stream << Id::RejectTransactions << uint8_t(resume.size());
        for (const auto& item : resume) {
            stream << item.first << item.second;
        }
        node.reportEvent(bin_pack);
    }
}

/*static*/
std::map<Reject::Reason, uint16_t> EventReport::parseReject(const cs::Bytes& bin_pack) {
    std::map<Reject::Reason, uint16_t> resume;
    cs::DataStream stream(bin_pack.data(), bin_pack.size());
    Id id = Id::None;
    stream >> id;
    if (id == Id::RejectTransactions) {
        uint8_t size = 0;
        Reject::Reason r = Reject::Reason::None;
        uint16_t cnt = 0;
        stream >> size;
        for (uint8_t i = 0; i < size; ++i) {
            stream >> r >> cnt;
            if (cnt == 0) {
                break;
            }
            resume[r] = cnt;
        }
    }
    return resume;
}

/*static*/
EventReport::Id EventReport::getId(const cs::Bytes& bin_pack) {
    if (bin_pack.empty()) {
        return Id::None;
    }
    return (EventReport::Id) bin_pack.front();
}

//void EventReport::parse(const cs::Bytes& bin_pack) {
//    const Id id = getId(bin_pack);
//    if (id == Id::RejectTransactions) {
//        auto resume = parseReject(bin_pack);
//        if (!resume.empty()) {
//            csdebug() << 
//        }
//    }
//}

