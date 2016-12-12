﻿#include <chrono>
#include <set>
#include <thread>
#include "qcoreapplication.h"
#include <qdebug.h>
#include <qthread.h>
#include "spdlog/spdlog.h"

#include "trader.h"
#include "myevent.h"
#include "position.h"
#include "struct.h"

using namespace std;
using namespace spdlog::level;

Trader::Trader(QObject *parent) : QObject(parent)
{
    init();
    //tdapi->Join();
}

Trader::Trader(const string &frontAddress, const string &brokerID, const string &userID, const string &password)
    : QObject(Q_NULLPTR), FrontAddress(frontAddress), BROKER_ID(brokerID), USER_ID(userID), PASSWORD(password)
{
    init();
}

Trader::~Trader() {

}

void Trader::init()
{
    setLogger();

    logger(info, "Initializing Trader");
    reqConnect();
    logger(info, "Initializing Trader Finished");
}

void Trader::reqConnect()
{
    tdapi = CThostFtdcTraderApi::CreateFtdcTraderApi();
    tdapi->RegisterSpi(this);
    tdapi->SubscribePublicTopic(THOST_TERT_RESTART);
    tdapi->SubscribePrivateTopic(THOST_TERT_QUICK);
    char *front = new char[FrontAddress.length() + 1];
    strcpy(front, FrontAddress.c_str());
    tdapi->RegisterFront(front);
    delete[] front;
    tdapi->Init();
}

int Trader::login()
{
    logger(info, "Trader Login...");
    emit sendToTraderMonitor("Trader Login...");
    auto loginField = new CThostFtdcReqUserLoginField();
    strcpy(loginField->BrokerID, BROKER_ID.c_str());
    strcpy(loginField->UserID, USER_ID.c_str());
    strcpy(loginField->Password, PASSWORD.c_str());
    int ret = tdapi->ReqUserLogin(loginField, ++nRequestID);
    showApiReturn(ret, "--> ReqLogin:", "ReqLogin Failed: ");
    return ret;
}

int Trader::logout()
{
    logger(info, "Trader Logout...");
    emit sendToTraderMonitor("Trader Logout...");
    auto logoutField = new CThostFtdcUserLogoutField();
    strcpy(logoutField->BrokerID, BROKER_ID.c_str());
    strcpy(logoutField->UserID, USER_ID.c_str());
    int ret = tdapi->ReqUserLogout(logoutField, ++nRequestID);
    showApiReturn(ret, "--> ReqLogout:", "ReqLogout Failed: ");
    return ret;
}

void Trader::OnFrontConnected()
{
    /*chrono::seconds sleepDuration(1);
    this_thread::sleep_for(sleepDuration);*/
    logger(info, "Trader Front Connected.");
    emit sendToTraderMonitor("Trader Front Connected.", Qt::green);
    this->login();
}

void Trader::OnFrontDisconnected(int nReason)
{
    QString msg = "Front Disconnected. Reason: ";
    switch (nReason)
    {
    case 0x1001:
        msg += u8"网络读失败";
        break;
    case 0x1002:
        msg += u8"网络写失败";
        break;
    case 0x2001:
        msg += u8"接收心跳超时";
        break;
    case 0x2002:
        msg += u8"发送心跳失败";
        break;
    case 0x2003:
        msg += u8"收到错误报文";
        break;
    default:
        break;
    }
    logger(err, msg.toLocal8Bit());
    emit sendToTraderMonitor(msg);
}

void Trader::OnRspUserLogin(CThostFtdcRspUserLoginField *pRspUserLogin, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
    if (!isErrorRspInfo(pRspInfo, "ReqUserLogin Failed: ")) {
        //if ((pRspInfo != nullptr && pRspInfo->ErrorID == 0) && pRspUserLogin != nullptr) {
        // TODO: watch MaxOrderRef returned in diff sessions.
        nMaxOrderRef = atoi(pRspUserLogin->MaxOrderRef);
        FrontID = pRspUserLogin->FrontID;
        SessionID = pRspUserLogin->SessionID;
        tradingDay = pRspUserLogin->TradingDay;

        QString msg;
        msg.append("Trader Login Successful.");
        msg.append("\n....TradingDay  = ").append(pRspUserLogin->TradingDay);
        msg.append("\n....LoginTime   = ").append(pRspUserLogin->LoginTime);
        msg.append("\n....SystemName  = ").append(pRspUserLogin->SystemName);
        msg.append("\n....UserID      = ").append(pRspUserLogin->UserID);
        msg.append("\n....BrokerID    = ").append(pRspUserLogin->BrokerID);
        msg.append("\n....SessionID   = ").append(QString::number(pRspUserLogin->SessionID));
        msg.append("\n....FrontID     = ").append(QString::number(pRspUserLogin->FrontID));
        msg.append("\n....System Time = ").append(pRspUserLogin->SystemName);
        msg.append("\n....SHFE Time   = ").append(pRspUserLogin->SHFETime);
        msg.append("\n....DCE  Time   = ").append(pRspUserLogin->DCETime);
        msg.append("\n....CZCE Time   = ").append(pRspUserLogin->CZCETime);
        msg.append("\n....FFEX Time   = ").append(pRspUserLogin->FFEXTime);
        emit sendToTraderMonitor(msg);
        logger(info, msg.toStdString().c_str());

        // TODO: move workflow to portfolio's Timer.
        // login workflow #1
        QThread::sleep(1);
        ReqQrySettlementInfo();
    }
}

void Trader::OnRspUserLogout(CThostFtdcUserLogoutField *pUserLogout, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
    if (!isErrorRspInfo(pRspInfo, "Trader Logout Failed: ")) {
        logger(info, "Trader Logout Success");
        emit sendToTraderMonitor("Trader Logout Success");
    }
}

void Trader::OnRspSettlementInfoConfirm(CThostFtdcSettlementInfoConfirmField *pSettlementInfoConfirm, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
    if (isErrorRspInfo(pRspInfo, "RspSettlementInfoConfirm: ")) {
        QString msg;
        msg += QString("Settlement Info Confirmed. ConfirmDate=%1 ConfirmTime=%2").arg(pSettlementInfoConfirm->ConfirmDate, pSettlementInfoConfirm->ConfirmTime);
        logger(info, msg.toStdString().c_str());
        emit sendToTraderMonitor(msg);
    }
}

void Trader::OnRspQrySettlementInfo(CThostFtdcSettlementInfoField *pSettlementInfo, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
    if (!isErrorRspInfo(pRspInfo, "RspQrySettlementInfo: ")) {
        if (isNewSettlementInfo) {
            strSettlementInfo = "";
            isNewSettlementInfo = false;
        }
        strSettlementInfo += pSettlementInfo->Content;
        if (bIsLast) {
            isNewSettlementInfo = true;
            if (strSettlementInfo == "") {
                emit sendToTraderMonitor(QString("No Settlement Info Retrieved."));
                logger(critical, "No Settlement Info Retrieved.");
            }
            else
                emit sendToTraderMonitor(QString::fromLocal8Bit(strSettlementInfo.c_str()));
//                emit sendToTraderMonitor(strSettlementInfo.c_str());

            // login workflow #2
            QThread::sleep(1);
            ReqQrySettlementInfoConfirm();
        }
    }
}

void Trader::OnRspQrySettlementInfoConfirm(CThostFtdcSettlementInfoConfirmField *pSettlementInfoConfirm, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
    //ReqSettlementInfoConfirm();
    if (!isErrorRspInfo(pRspInfo, "RspQrySettlementInfoConfirm: ")) {
		if (pSettlementInfoConfirm != nullptr) {
			QString msg;
			msg += QString("RspQrySettlementInfoConfirm: ConfirmDate=%1 ConfirmTime=%2").arg(pSettlementInfoConfirm->ConfirmDate, pSettlementInfoConfirm->ConfirmTime);
			logger(info, msg.toStdString().c_str());
			emit sendToTraderMonitor(msg);
			if (pSettlementInfoConfirm->ConfirmDate != tradingDay) {
				ReqSettlementInfoConfirm();
			}
		}
    }
    // login workflow #3
    QThread::sleep(1);
    ReqQryInstrument();
}

void Trader::OnRspOrderInsert(CThostFtdcInputOrderField *pInputOrder, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
    if (!isErrorRspInfo(pRspInfo, "Thost RspOrderInsert Failed: "))
        logger(info, "Thost RspOrderInsert Success: OrderRef={}", pInputOrder->OrderRef);
}

void Trader::OnRspOrderAction(CThostFtdcInputOrderActionField *pInputOrderAction, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
    if (!isErrorRspInfo(pRspInfo, "Thost RspOrderAction Failed: "))
        logger(info, "Thost RspOrderAction Success: OrderRef={}", pInputOrderAction->OrderRef);
}

void Trader::OnRspQryOrder(CThostFtdcOrderField *pOrder, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
    if (!isErrorRspInfo(pRspInfo, "RspQryOrder: ")) {
        if (pOrder != nullptr) {
            QString msg;
            msg.append("OrderRef=").append(pOrder->OrderRef);
            msg.append(" ").append(pOrder->InstrumentID);
            msg.append(" Status=").append(pOrder->OrderStatus);
            msg.append(" Offset=").append(pOrder->CombOffsetFlag[0]);
            msg.append(" Direction=").append(pOrder->Direction);
            //msg.append(" PriceType=").append(pOrder->OrderPriceType);
            msg.append(" LmtPx=").append(QString::number(pOrder->LimitPrice));
            msg.append(" Total=").append(QString::number(pOrder->VolumeTotal));
            msg.append(" Original=").append(QString::number(pOrder->VolumeTotalOriginal));
            //msg.append(" SessionID=").append(QString::number(pOrder->SessionID));
            //msg.append(" OrderSysID=").append(pOrder->OrderSysID);
            //msg.append(" TraderID=").append(pOrder->TraderID);
            //msg.append(" OrderLocalID=").append(pOrder->OrderLocalID);
            //msg.append(" ActiveTime=").append(pOrder->ActiveTime);
            emit sendToTraderMonitor(msg);

            auto fcpy = new CThostFtdcOrderField;
            memcpy(fcpy, pOrder, sizeof(CThostFtdcOrderField));
            auto orderEvent = new MyEvent(OrderEvent, fcpy);
            QCoreApplication::postEvent(dispatcher, orderEvent);
        }
        if (bIsLast)
        {
            logger(info, "Qry Order Finished.");
            emit sendToTraderMonitor("Query Order Finished.");
        }
    }
}

void Trader::OnRspQryTrade(CThostFtdcTradeField *pTrade, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
    if (!isErrorRspInfo(pRspInfo, "RspQryTrade: ")) {
        if (pTrade != nullptr) {
            QString msg;
            msg.append(pTrade->InstrumentID);
            msg.append(" Offset=").append(pTrade->OffsetFlag);
            msg.append(" Direction=").append(pTrade->Direction);
            msg.append(" Price=").append(QString::number(pTrade->Price));
            msg.append(" Volume=").append(QString::number(pTrade->Volume));
            msg.append(" TradeTime=").append(pTrade->TradeTime);
            emit sendToTraderMonitor(msg);
        }
        if (bIsLast) {
            logger(info, "Qry Trade Finished.");
            emit sendToTraderMonitor("Qry Trade Finished.");
        }
    }
}

void Trader::OnRspQryInvestorPosition(CThostFtdcInvestorPositionField *pInvestorPosition, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
    if (!isErrorRspInfo(pRspInfo, "RspQryPosition: ")) {
        if (pInvestorPosition != nullptr) {
            QString msg;
            msg.append(pInvestorPosition->InstrumentID);
            msg.append(" Direction=").append(pInvestorPosition->PosiDirection);
            msg.append(" Position=").append(QString::number(pInvestorPosition->Position));
            msg.append(" PL=").append(QString::number(pInvestorPosition->PositionProfit));
            msg.append(" ClsAmount=").append(QString::number(pInvestorPosition->CloseAmount));
            msg.append(" Margin=").append(QString::number(pInvestorPosition->UseMargin));
            emit sendToTraderMonitor(msg);

            auto fcpy = new CThostFtdcInvestorPositionField;
            memcpy(fcpy, pInvestorPosition, sizeof(CThostFtdcInvestorPositionField));
            auto posEvent = new MyEvent(PositionEvent, fcpy);
            QCoreApplication::postEvent(dispatcher, posEvent);
        }
        if (bIsLast) {
            logger(info, "Qry InvestorPosition Finished");
            emit sendToTraderMonitor("Qry InvestorPosition Finished.");

            // Send isLast signal event
            auto fcpy = new CThostFtdcInvestorPositionField;
            memset(fcpy, 0, sizeof(CThostFtdcInvestorPositionField));
            auto posEvent = new MyEvent(PositionEvent, fcpy);
            QCoreApplication::postEvent(dispatcher, posEvent);
        }
    }
}

void Trader::OnRspQryInvestorPositionDetail(CThostFtdcInvestorPositionDetailField *pInvestorPositionDetail, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
    if (!isErrorRspInfo(pRspInfo, "RspQryPositionDetail: ")) {
        if (pInvestorPositionDetail != nullptr) {
            QString msg;
            msg.append(pInvestorPositionDetail->InstrumentID);
            msg.append(" Direction=").append(pInvestorPositionDetail->Direction);
            msg.append(" Position=").append(QString::number(pInvestorPositionDetail->Volume));
            msg.append(" PL=").append(QString::number(pInvestorPositionDetail->CloseProfitByDate));
            msg.append(" ClsVolume=").append(QString::number(pInvestorPositionDetail->CloseVolume));
            msg.append(" Margin=").append(QString::number(pInvestorPositionDetail->Margin));
            emit sendToTraderMonitor(msg);

            auto fcpy = new CThostFtdcInvestorPositionDetailField;
            memcpy(fcpy, pInvestorPositionDetail, sizeof(CThostFtdcInvestorPositionDetailField));
            auto posDetailEvent = new MyEvent(PositionDetailEvent, fcpy);
            QCoreApplication::postEvent(dispatcher, posDetailEvent);
        }
        if (bIsLast) {
            logger(info, "Qry InvestorPositionDetail Finished");
            emit sendToTraderMonitor("Qry InvestorPositionDetail Finished.");

            // Send isLast signal event
            auto fcpy = new CThostFtdcInvestorPositionDetailField;
            memset(fcpy, 0, sizeof(CThostFtdcInvestorPositionDetailField));
            auto posDetailEvent = new MyEvent(PositionDetailEvent, fcpy);
            QCoreApplication::postEvent(dispatcher, posDetailEvent);
        }
    }
}

void Trader::OnRspQryTradingAccount(CThostFtdcTradingAccountField *pTradingAccount, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
    if (!isErrorRspInfo(pRspInfo, "RspQryTradingAccount: ")) {
        if (bIsLast) {
            QString msg = "Trading Account";
            msg.append("\n....Available  = ").append(QString::number(pTradingAccount->Available));
            msg.append("\n....Balance    = ").append(QString::number(pTradingAccount->Balance));
            msg.append("\n....CurrMargin = ").append(QString::number(pTradingAccount->CurrMargin));
            msg.append("\n....Reserve    = ").append(QString::number(pTradingAccount->Reserve));
            logger(info, msg.toStdString().c_str());
            emit sendToTraderMonitor(msg);

            auto fcpy = new CThostFtdcTradingAccountField;
            memcpy(fcpy, pTradingAccount, sizeof(CThostFtdcTradingAccountField));
            auto accInfoEvent = new MyEvent(AccountInfoEvent, fcpy);
            QCoreApplication::postEvent(dispatcher, accInfoEvent);

            // login workflow #5
            QThread::sleep(1);
            ReqQryInvestorPositionDetail();
        }
    }
}

void Trader::OnRspQryInstrument(CThostFtdcInstrumentField *pInstrument, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
    if (!isErrorRspInfo(pRspInfo, "RspQryInstrument: ")) {
        if (pInstrument != nullptr) {
            //QString msg;
            //msg.append(pInstrument->InstrumentID);
            //msg.append(" ").append(pInstrument->ExchangeID);
            //msg.append(" ").append(pInstrument->ExpireDate);
            //emit sendToTraderMonitor(msg);
            auto fcpy = new CThostFtdcInstrumentField;
            memcpy(fcpy, pInstrument, sizeof(CThostFtdcInstrumentField));
            auto contractInfoEvent = new MyEvent(ContractInfoEvent, fcpy);
            QCoreApplication::postEvent(dispatcher, contractInfoEvent);

            if (bIsLast) {
                // login workflow #4
                QThread::sleep(1);
                ReqQryTradingAccount();
            }
        }
    }
}

void Trader::OnRspQryInstrumentCommissionRate(CThostFtdcInstrumentCommissionRateField *pInstrumentCommissionRate, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
    // TODO: RETURN NULL
}

void Trader::OnRspQryDepthMarketData(CThostFtdcDepthMarketDataField *pDepthMarketData, CThostFtdcRspInfoField *pRspInfo, int nRequestID, bool bIsLast)
{
    if (!isErrorRspInfo(pRspInfo, "RspQryDepthMarketData: ")) {
        auto fcpy = new CThostFtdcDepthMarketDataField;
        memcpy(fcpy, pDepthMarketData, sizeof(CThostFtdcDepthMarketDataField));
        auto feedEvent = new MyEvent(FeedEvent, fcpy);
        QCoreApplication::postEvent(dispatcher, feedEvent);
    }
}

void Trader::OnRtnOrder(CThostFtdcOrderField *pOrder)
{
    if (pOrder != nullptr) {
        QString msg;
        msg += QString("OnRtnOrder: OrderRef=%1, %2, StatusMsg=%3").arg(pOrder->OrderRef, pOrder->InstrumentID, QString::fromLocal8Bit(pOrder->StatusMsg));
        //msg.append(" ExchangeID=").append(pOrder->ExchangeID);
        //msg.append(" InsertTime=").append(pOrder->InsertTime);
        //msg.append(" FrontID=").append(QString::number(pOrder->FrontID));
        //msg.append(" SessionID=").append(QString::number(pOrder->SessionID));
        //msg.append(" TraderID=").append(pOrder->TraderID);
        //msg.append(" OrderLocalID=").append(pOrder->OrderLocalID);
        //msg.append(" OrderSysID=").append(pOrder->OrderSysID);
        emit sendToTraderMonitor(msg);

        //qDebug() << QString(pOrder->StatusMsg).toStdString().c_str();
        //logger(info, "OnRtnOrder: OrderRef={}, Status={}, Status Msg={}", pOrder->OrderRef, pOrder->OrderStatus, pOrder->StatusMsg);
        logger(info, msg.toStdString().c_str());

        auto fcpy = new CThostFtdcOrderField;
        memcpy(fcpy, pOrder, sizeof(CThostFtdcOrderField));
        auto orderEvent = new MyEvent(OrderEvent, fcpy);
        QCoreApplication::postEvent(dispatcher, orderEvent);
    }
    else
        logger(err, "OnRtnOrder nullptr or null data");
}

void Trader::OnRtnTrade(CThostFtdcTradeField *pTrade)
{
    if (pTrade != nullptr) {
        QString msg;
        msg += QString("OnRtnTrade: OrderRef=%1, %2").arg(pTrade->OrderRef, pTrade->InstrumentID);
        //msg.append("\nExchangeID=").append(pTrade->ExchangeID);
        //msg.append(" TraderID=").append(pTrade->TraderID);
        //msg.append(" OrderSysID=").append(pTrade->OrderSysID);
        //msg.append(" OrderRef=").append(pTrade->OrderRef);
        //msg.append("\n").append(pTrade->InstrumentID);
        msg.append(" Dir=").append(mymap::direction_char.at(pTrade->Direction));
        msg.append(" Offset=").append(mymap::offsetFlag_string.at(pTrade->OffsetFlag).c_str());
        msg.append(" Price=").append(QString::number(pTrade->Price));
        msg.append(" Volume=").append(QString::number(pTrade->Volume));
        msg.append(" T=").append(pTrade->TradeTime);
        logger(info, msg.toStdString().c_str());
        emit sendToTraderMonitor(msg);

        auto fcpy = new CThostFtdcTradeField;
        memcpy(fcpy, pTrade, sizeof(CThostFtdcTradeField));
        auto tradeEvent = new MyEvent(TradeEvent, fcpy);
        QCoreApplication::postEvent(dispatcher, tradeEvent);
    }
    else
        logger(err, "OnRtnTrade nullptr or null data");
}

void Trader::OnErrRtnOrderInsert(CThostFtdcInputOrderField *pInputOrder, CThostFtdcRspInfoField *pRspInfo)
{
    QString msg = QString("ErrRtnOrderInsert: OrderRef=%1 ").arg(pInputOrder->OrderRef);
    isErrorRspInfo(pRspInfo, msg.toStdString().c_str());
}

void Trader::OnErrRtnOrderAction(CThostFtdcOrderActionField * pOrderAction, CThostFtdcRspInfoField * pRspInfo)
{
    QString msg = QString("ErrRtnOrderAction: OrderRef=%1 ").arg(pOrderAction->OrderRef);
    isErrorRspInfo(pRspInfo, msg.toStdString().c_str());
}

int Trader::ReqSettlementInfoConfirm()
{
    auto info = new CThostFtdcSettlementInfoConfirmField();
    strcpy(info->BrokerID, BROKER_ID.c_str());
    strcpy(info->InvestorID, USER_ID.c_str());
    int ret = tdapi->ReqSettlementInfoConfirm(info, ++nRequestID);
    showApiReturn(ret, "--> ReqSettlementInfoConfirm", "--x ReqSettlementInfoConfirm Failed");
    return ret;
}

int Trader::ReqQrySettlementInfoConfirm()
{
    auto info = new CThostFtdcQrySettlementInfoConfirmField();
    strcpy(info->BrokerID, BROKER_ID.c_str());
    strcpy(info->InvestorID, USER_ID.c_str());
    int ret = tdapi->ReqQrySettlementInfoConfirm(info, ++nRequestID);
    showApiReturn(ret, "--> ReqQrySettlementInfoConfirm", "ReqQrySettlementInfoConfirm Failed");
    return ret;
}

int Trader::ReqQrySettlementInfo(string TradingDay)
{
    auto info = new CThostFtdcQrySettlementInfoField();
    strcpy(info->BrokerID, BROKER_ID.c_str());
    strcpy(info->InvestorID, USER_ID.c_str());
    strcpy(info->TradingDay, TradingDay.c_str());
    int ret = tdapi->ReqQrySettlementInfo(info, ++nRequestID);
    showApiReturn(ret, "--> ReqQrySettlementInfo", "ReqQrySettlementInfo Failed");
    return ret;
}

int Trader::ReqOrderInsert(CThostFtdcInputOrderField *pInputOrder)
{
    int ret = tdapi->ReqOrderInsert(pInputOrder, ++nRequestID);
    showApiReturn(ret, "--> ReqOrderInsert", "ReqOrderInsert Sent Error");
    return ret;
}

// Limit Order
int Trader::ReqOrderInsert(string InstrumentID, EnumOffsetFlagType OffsetFlag, EnumDirectionType Direction, double Price, int Volume)
{
    auto order = new CThostFtdcInputOrderField();
    strcpy(order->BrokerID, BROKER_ID.c_str());
    strcpy(order->UserID, USER_ID.c_str());
    strcpy(order->InvestorID, USER_ID.c_str());
    strcpy(order->OrderRef, QString::number(++nMaxOrderRef).toStdString().c_str());
    order->ContingentCondition = Immediately;
    order->ForceCloseReason = NotForceClose;
    order->IsAutoSuspend = false;
    order->MinVolume = 1;
    order->OrderPriceType = LimitPrice;
    order->TimeCondition = GFD;
    order->UserForceClose = false;
    order->VolumeCondition = AV;
    order->CombHedgeFlag[0] = Speculation;

    strcpy(order->InstrumentID, InstrumentID.c_str());
    order->CombOffsetFlag[0] = OffsetFlag;
    order->Direction = Direction;
    order->LimitPrice = Price;
    order->VolumeTotalOriginal = Volume;

    int ret = tdapi->ReqOrderInsert(order, ++nRequestID);
    showApiReturn(ret, "--> LimitOrderInsert", "LimitOrderInsert Sent Error");
    return ret;
}

// Market Order
int Trader::ReqOrderInsert(string InstrumentID, EnumOffsetFlagType OffsetFlag, EnumDirectionType Direction, int Volume)
{
    auto order = new CThostFtdcInputOrderField();
    strcpy(order->BrokerID, BROKER_ID.c_str());
    strcpy(order->UserID, USER_ID.c_str());
    strcpy(order->InvestorID, USER_ID.c_str());
    //strcpy(order->OrderRef, QString::number(++nMaxOrderRef).toStdString().c_str());
    order->ContingentCondition = Immediately;
    order->ForceCloseReason = NotForceClose;
    order->IsAutoSuspend = false;
    order->MinVolume = 1;
    order->OrderPriceType = AnyPrice;
    order->TimeCondition = IOC; //立即完成，否则撤销
    order->UserForceClose = false;
    order->VolumeCondition = AV;
    order->CombHedgeFlag[0] = Speculation;

    strcpy(order->InstrumentID, InstrumentID.c_str());
    order->CombOffsetFlag[0] = OffsetFlag;
    order->Direction = Direction;
    order->LimitPrice = 0;
    order->VolumeTotalOriginal = Volume;

    int ret = tdapi->ReqOrderInsert(order, ++nRequestID);
    showApiReturn(ret, "--> MarketOrderInsert", "MarketOrderInsert Sent Error");
    return ret;
}

// Condition Order
int Trader::ReqOrderInsert(string InstrumentID, EnumContingentConditionType ConditionType, double conditionPrice,
    EnumOffsetFlagType OffsetFlag, EnumDirectionType Direction, EnumOrderPriceTypeType PriceType, double Price, int Volume)
{
    auto order = new CThostFtdcInputOrderField();
    strcpy(order->BrokerID, BROKER_ID.c_str());
    strcpy(order->UserID, USER_ID.c_str());
    strcpy(order->InvestorID, USER_ID.c_str());
    //strcpy(order->OrderRef, QString::number(++nMaxOrderRef).toStdString().c_str());
    order->ForceCloseReason = NotForceClose;
    order->IsAutoSuspend = false;
    order->MinVolume = 1;
    order->OrderPriceType = AnyPrice;
    order->TimeCondition = IOC; //立即完成，否则撤销
    order->UserForceClose = false;
    order->VolumeCondition = AV;
    order->CombHedgeFlag[0] = Speculation;

    strcpy(order->InstrumentID, InstrumentID.c_str());
    order->ContingentCondition = ConditionType;
    order->StopPrice = conditionPrice;
    order->CombOffsetFlag[0] = OffsetFlag;
    order->Direction = Direction;
    order->OrderPriceType = PriceType;
    order->LimitPrice = Price;  // Effective only if PriceType == LimitPrice
    order->VolumeTotalOriginal = Volume;

    int ret = tdapi->ReqOrderInsert(order, ++nRequestID);
    showApiReturn(ret, "--> MarketOrderInsert", "MarketOrderInsert Sent Error");
    return ret;
}

int Trader::ReqOrderAction(CThostFtdcInputOrderActionField *pInputOrderAction)
{
    int ret = tdapi->ReqOrderAction(pInputOrderAction, ++nRequestID);
    showApiReturn(ret, "--> ReqOrderAction", "ReqOrderAction Sent Error");
    return ret;
}

int Trader::ReqOrderAction(string InstrumentID, string OrderRef)
{
    auto action = new CThostFtdcInputOrderActionField();
    action->ActionFlag = THOST_FTDC_AF_Delete;
    strcpy(action->BrokerID, BROKER_ID.c_str());
    strcpy(action->InvestorID, USER_ID.c_str());
    action->FrontID = FrontID;
    action->SessionID = SessionID;
    strcpy(action->InstrumentID, InstrumentID.c_str());
    //strcpy(action->OrderRef, OrderRef.c_str());
    // TODO: check self set orderRef's format!
    if (OrderRef != "")
    {
        int n = OrderRef.length();
        // OrderRef has format "     xxxxx", length set 12 here.
        QString tmp{ 12 - n,' ' };
        strcpy(action->OrderRef, tmp.append(OrderRef.c_str()).toStdString().c_str());
    }
    return ReqOrderAction(action);
}

int Trader::ReqOrderAction(string InstrumentID, int FrontID, int SessionID, string OrderRef, string ExchangeID, string OrderSysID)
{
    auto action = new CThostFtdcInputOrderActionField();
    action->ActionFlag = THOST_FTDC_AF_Delete;
    strcpy(action->BrokerID, BROKER_ID.c_str());
    strcpy(action->InstrumentID, InstrumentID.c_str());
    strcpy(action->InvestorID, USER_ID.c_str());

    if (FrontID != 0)
        action->FrontID = FrontID;
    if (SessionID != 0)
        action->SessionID = SessionID;
    if (OrderRef != "")
    {
        //strcpy(action->OrderRef, OrderRef.c_str());
        int n = OrderRef.length();
        // OrderRef has format "     xxxxx", length set 12 here.
        QString tmp{ 12 - n,' ' };
        strcpy(action->OrderRef, tmp.append(OrderRef.c_str()).toStdString().c_str());
    }
    if (ExchangeID != "")
        strcpy(action->ExchangeID, ExchangeID.c_str());
    if (OrderSysID != "")
    {
        int n = OrderSysID.length();
        // OrderSysID has format "     xxxxx", length set 12 here.
        QString tmp{ 12 - n,' ' };
        strcpy(action->OrderSysID, tmp.append(OrderSysID.c_str()).toStdString().c_str());
    }
    return ReqOrderAction(action);
}

int Trader::ReqQryOrder(string InstrumentID, string ExchangeID, string timeStart, string timeEnd, string OrderSysID)
{
    auto order = new CThostFtdcQryOrderField();
    strcpy(order->BrokerID, BROKER_ID.c_str());
    strcpy(order->InstrumentID, InstrumentID.c_str());
    strcpy(order->InvestorID, USER_ID.c_str());
    strcpy(order->ExchangeID, ExchangeID.c_str());
    strcpy(order->OrderSysID, OrderSysID.c_str());
    strcpy(order->InsertTimeStart, timeStart.c_str());
    strcpy(order->InsertTimeEnd, timeEnd.c_str());
    int ret = tdapi->ReqQryOrder(order, ++nRequestID);
    showApiReturn(ret, "--> ReqQryOrder", "--x ReqQryOrder Failed");
    return ret;
}

int Trader::ReqQryTrade(string timeStart = "", string timeEnd = "", string InstrumentID = "", string ExchangeID = "", string TradeID = "")
{
    auto trade = new CThostFtdcQryTradeField();
    strcpy(trade->BrokerID, BROKER_ID.c_str());
    strcpy(trade->InvestorID, USER_ID.c_str());
    strcpy(trade->InstrumentID, InstrumentID.c_str());
    strcpy(trade->ExchangeID, ExchangeID.c_str());
    strcpy(trade->TradeID, TradeID.c_str());
    strcpy(trade->TradeTimeStart, timeStart.c_str());
    strcpy(trade->TradeTimeEnd, timeEnd.c_str());
    int ret = tdapi->ReqQryTrade(trade, ++nRequestID);
    showApiReturn(ret, "--> ReqQryTrade", "--x ReqQryTrade Failed");
    return ret;
}

int Trader::ReqQryDepthMarketData(string InstrumentID)
{
    auto f = new CThostFtdcQryDepthMarketDataField();
    strcpy(f->InstrumentID, InstrumentID.c_str());
    int ret = tdapi->ReqQryDepthMarketData(f, ++nRequestID);
    showApiReturn(ret, "--> ReqQryDepthMarketData", "--x ReqQryDepthMarketData Failed");
    return ret;
}

int Trader::ReqQryTradingAccount()
{
    auto acc = new CThostFtdcQryTradingAccountField();
    strcpy(acc->BrokerID, BROKER_ID.c_str());
    strcpy(acc->InvestorID, USER_ID.c_str());
    int ret = tdapi->ReqQryTradingAccount(acc, ++nRequestID);
    showApiReturn(ret, "--> ReqQryTradingAccount", "--x ReqQryTradingAccount Failed");
    return ret;
}

int Trader::ReqQryInvestorPosition(string InstrumentID)
{
    auto pos = new CThostFtdcQryInvestorPositionField();
    strcpy(pos->BrokerID, BROKER_ID.c_str());
    strcpy(pos->InvestorID, USER_ID.c_str());
    strcpy(pos->InstrumentID, InstrumentID.c_str());
    int ret = tdapi->ReqQryInvestorPosition(pos, ++nRequestID);
    showApiReturn(ret, "--> ReqQryInvestorPosition", "--x ReqQryInvestorPosition Failed");
    return ret;
}

int Trader::ReqQryInvestorPositionDetail(string InstrumentID)
{
    auto pos = new CThostFtdcQryInvestorPositionDetailField();
    strcpy(pos->BrokerID, BROKER_ID.c_str());
    strcpy(pos->InvestorID, USER_ID.c_str());
    strcpy(pos->InstrumentID, InstrumentID.c_str());
    int ret = tdapi->ReqQryInvestorPositionDetail(pos, ++nRequestID);
    showApiReturn(ret, "--> ReqQryInvestorPositionDetail", "--x ReqQryInvestorPositionDetail Failed");
    return ret;
}

int Trader::ReqQryInstrument()
{
    auto field = new CThostFtdcQryInstrumentField();
    int ret = tdapi->ReqQryInstrument(field, ++nRequestID);
    showApiReturn(ret, "--> ReqQryInstrument", "--x ReqQryInstrument Failed");
    return ret;
}

int Trader::ReqQryInstrumentMarginRate(string InstrumentID, EnumHedgeFlagType hedgeFlag)
{
    auto field = new CThostFtdcQryInstrumentMarginRateField();
    strcpy(field->BrokerID, BROKER_ID.c_str());
    strcpy(field->InvestorID, USER_ID.c_str());
    strcpy(field->InstrumentID, InstrumentID.c_str());
    field->HedgeFlag = hedgeFlag;
    int ret = tdapi->ReqQryInstrumentMarginRate(field, ++nRequestID);
    showApiReturn(ret, "--> ReqQryInstrumentMarginRate", "--x ReqQryInstrumentMarginRate Failed");
    return ret;
}

int Trader::ReqQryInstrumentCommissionRate(string InstrumentID)
{
    auto field = new CThostFtdcQryInstrumentCommissionRateField();
    strcpy(field->BrokerID, BROKER_ID.c_str());
    strcpy(field->InvestorID, USER_ID.c_str());
    strcpy(field->InstrumentID, InstrumentID.c_str());
    int ret = tdapi->ReqQryInstrumentCommissionRate(field, ++nRequestID);
    showApiReturn(ret, "--> ReqQryInstrumentCommisionRate", "--x ReqQryInstrumentCommisionRate Failed");
    return ret;
}

void Trader::handleDispatch(int tt)
{
    qDebug() << "RECEIVED EVENT signal****************";
}

void Trader::setLogger()
{
    //console = spdlog::get("console");
    console = spdlog::stdout_color_mt("trader ");
    console->set_pattern("[%H:%M:%S.%f] [%n] [%L] %v");
    g_logger = spdlog::get("file_logger");
    trader_logger = spdlog::rotating_logger_mt("trader_logger", "logs/trader_log", 1024 * 1024 * 5, 3);
    //trader_logger = spdlog::daily_logger_mt("trader_logger", "logs/trader_log", 5, 0);
    trader_logger->flush_on(spdlog::level::info);
}

Dispatcher* Trader::getDispatcher()
{
    return dispatcher;
}

void Trader::setDispatcher(Dispatcher *ee)
{
    dispatcher = ee;
}

string Trader::getTradingDay()
{
    return tradingDay;
}

// Note: not restoring and showing corresponding nRequestID. can be implemented if need.
bool Trader::isErrorRspInfo(CThostFtdcRspInfoField *pRspInfo, const char *msg)
{
    bool isError = (pRspInfo) && (pRspInfo->ErrorID != 0);
    if (isError) {
        QString errMsg = QString(msg).append("ErrorID=").append(QString::number(pRspInfo->ErrorID)).append(", ErrorMsg=").append(QString::fromLocal8Bit(pRspInfo->ErrorMsg));
//        logger(err, "{}ErrorID={}, ErrorMsg={}", msg, pRspInfo->ErrorID, pRspInfo->ErrorMsg);
        logger(err, errMsg.toStdString().c_str());
        emit sendToTraderMonitor(errMsg);
    }
    return isError;
}


void Trader::showApiReturn(int ret, QString outputIfSuccess, QString outputIfError)
{
    if (outputIfSuccess != "" || outputIfError != "") {
        QString msg;
        switch (ret) {
        case 0:
            //msg = outputIfSuccess.append("0: Sent successfully ").append(QString("ReqID=%1").arg(QString::number(nRequestID)));
            msg = outputIfSuccess.append(" | Api return 0: Sent successfully. ").append(QString("ReqID=%1").arg(nRequestID));
            logger(info, msg.toStdString().c_str());
            emit sendToTraderMonitor(msg);
            break;
        case -1:
            msg = outputIfError.append(" | Api return -1: Failed, network problem, ").append(QString("ReqID=%1").arg(nRequestID));
            logger(err, msg.toStdString().c_str());
            emit sendToTraderMonitor(msg);
            break;
        case -2:
            msg = outputIfError.append(" | Api return -2: waiting request queue pass limit. ").append(QString("ReqID=%1").arg(nRequestID));
            logger(err, msg.toStdString().c_str());
            emit sendToTraderMonitor(msg);
            break;
        case -3:
            msg = outputIfError.append(" | Api return -3: request/sec pass limit. ").append(QString("ReqID=%1").arg(nRequestID));
            logger(err, msg.toStdString().c_str());
            emit sendToTraderMonitor(msg);
            break;
        default:
            break;
        }
    }
}

//EnumOffsetFlagType str2OffsetFlagType(string str);
//EnumDirectionType str2DirectionType(string str);

void Trader::execCmdLine(QString cmdLine)
{
    //qDebug() << "Trader thread" << QObject::thread();
    QStringList argv(cmdLine.split(" "));
    int n = argv.count();
    if (n > 0) {
        if (argv.at(0) == "insert" || argv.at(0) == "ins") {
            if (n == 6) {
                string InstrumentID{ argv.at(1).toStdString() };
                //EnumOffsetFlagType OffsetFlag{ str2OffsetFlagType(argv.at(2).toStdString()) };
                std::set<string> ofStrSet = { "open", "o", "close", "c" };
                std::set<string> dirStrSet = { "buy", "b", "sell", "s" };
                EnumOffsetFlagType OffsetFlag;
                EnumDirectionType Direction;
                if (ofStrSet.find(argv.at(2).toStdString()) != ofStrSet.end())
                    OffsetFlag = mymap::string_offsetFlag.at(argv.at(2).toStdString());
                else {
                    emit sendToTraderMonitor("invalid cmd");
                    return;
                }
                if (dirStrSet.find(argv.at(3).toStdString()) != dirStrSet.end())
                    Direction = mymap::string_directionFlag.at(argv.at(3).toStdString());
                else {
                    emit sendToTraderMonitor("invalid cmd");
                    return;
                }
                bool okp, okv;
                double Price{ argv.at(4).toDouble(&okp) };
                int Volume{ argv.at(5).toInt(&okv) };
                if (okp && okv) {
                    ReqOrderInsert(InstrumentID, OffsetFlag, Direction, Price, Volume);
                }
                else {
                    emit sendToTraderMonitor("invalid cmd");
                    return;
                }
            }
        }
        else if (argv.at(0) == "infoconfirm") {
            ReqSettlementInfoConfirm();
        }
        else if (argv.at(0) == "cancel" || argv.at(0) == "x") {
            if (n == 4 && argv.at(1) == "sys") {
                string ExchangeID{ argv.at(2).toStdString() };
                string OrderSysID{ argv.at(3).toStdString() };

                ReqOrderAction("", 0, 0, "", ExchangeID, OrderSysID);
            }
            if (n == 4 && argv.at(1) == "ref") {
                string InstrmentID{ argv.at(2).toStdString() };
                string OrderRef{ argv.at(3).toStdString() };
                ReqOrderAction(InstrmentID, OrderRef);
            }
        }
        else if (argv.at(0) == "qorder" || argv.at(0) == "qod") {
            ReqQryOrder();
        }
        else if (argv.at(0) == "qtrade" || argv.at(0) == "qtd") {
            ReqQryTrade();
        }
        else if (argv.at(0) == "qpos") {
            ReqQryInvestorPosition();
        }
        else if (argv.at(0) == "qpdt") {
            ReqQryInvestorPositionDetail();
        }
        else if (argv.at(0) == "qmkt") {
            ReqQryDepthMarketData(argv.at(1).toStdString());
        }
        else if (argv.at(0) == "qcomm") {
            ReqQryInstrumentCommissionRate(argv.at(1).toStdString());
        }
        else if (argv.at(0) == "qaccount" || argv.at(0) == "qacc") {
            ReqQryTradingAccount();
        }
        else if (argv.at(0) == "qc" || argv.at(0) == "qinst") {
            ReqQryInstrument();
        }
        else if (argv.at(0) == "login") {
            login();
        }
        else if (argv.at(0) == "logout") {
            logout();
        }
        else {
            emit sendToTraderMonitor("Invalid cmd.");
        }
    }
}

//void Trader::showRspInfo(CThostFtdcRspInfoField *pRspInfo, const char *outputIfSuccess /*= ""*/, const char *outputIfError /*= ""*/)
//{
//    if (pRspInfo != nullptr) {
//        if (pRspInfo->ErrorID == 0 && outputIfSuccess != "") {
//            //logger(spdlog::level::info, outputIfSuccess.toStdString().c_str());
//            logger(info, "{} | Thost RspInfo: ErrorID={}, ErrorMsg={}", outputIfSuccess, pRspInfo->ErrorID, pRspInfo->ErrorMsg);
//            emit sendToTraderMonitor(outputIfSuccess);
//        }
//        else {
//            QString(outputIfError).append("\nErrorID=").append(QString::number(pRspInfo->ErrorID)).append("\tErrorMsg=").append(QString::fromLocal8Bit(pRspInfo->ErrorMsg));
//            //qDebug() << outputIfError.toStdString().c_str();
//            logger(warn, "{} | Thost RspInfo: ErrorID={}, ErrorMsg={}", outputIfError, pRspInfo->ErrorID, pRspInfo->ErrorMsg);
//            emit sendToTraderMonitor(outputIfError);
//        }
//    }
//    else
//        //qDebug() << "pRspInfo is nullptr";
//        logger(warn, "pRspInfo is nullptr, {}", outputIfError);
//}

//EnumOffsetFlagType str2OffsetFlagType(string str)
//{
//    if (str == "open" || str == "o") { return Open; }
//    else if (str == "close" || str == "c") { return Close; }
//    else if (str == "forceclose") { return ForceClose; }
//    else if (str == "closetoday") { return CloseToday; }
//    else if (str == "closeyesterday") { return CloseYesterday; }
//    else if (str == "forceoff") { return ForceOff; }
//    else if (str == "localforceclose") { return LocalForceClose; }
//    else return Open;  // not good
//}

//EnumDirectionType str2DirectionType(string str)
//{
//    if (str == "b" || str == "buy") { return Buy; }
//    else if (str == "s" || str == "sell") { return Sell; }
//    else return Buy;  // not good
//}