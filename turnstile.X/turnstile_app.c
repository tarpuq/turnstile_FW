/* 
 * File:   turnstile_app.c
 * Author:      gguzman
 * Comments:
 * Revision history: 
 */

#include "turnstile_app.h"

#include "mcc_generated_files/mcc.h"

#include "rfid_reader.h"

//  For AppConfig
#include "TCPIP Stack/TCPIP.h"
extern APP_CONFIG AppConfig;

static enum _MessageType
{
    MSG_ENTRY_REQUIRED = 0,
    MSG_ENTRY_RESULT,
    MSG_EXIT_REQUIRED,
    MSG_EXIT_RESULT
};

uint8_t validationMsg = 0;
uint8_t peopleCounter = 0;

void turnstileTask (void)
{
    static uint8_t entryExit = 0;
    static DWORD Timer;
    uint8_t dataBuffer[10];
    static uint24_t cardNumber;
    static uint8_t testRequired = 0;
    static uint8_t testTimeout = 0;
    static uint8_t testStatus = 0;
    uint8_t sendDataLen = 0;
    uint8_t devId;
    uint8_t validCard;
    static uint8_t msgType;

    static enum _TurnstileState
    {
        SM_INIT = 0,
        SM_WAITING_CARD,
        SM_PREPARE_FRAME,
        SM_WAIT_SERVER_RESPONSE,
        SM_CHECKING_ESD,
        SM_ACCESS_GRANTED,
        SM_ACCESS_DENIED,
        SM_WAITING_PASS,
        SM_RESTARTING,
        SM_IDLE
    } TurnstileState = SM_INIT;

    switch (TurnstileState)
    {
    case SM_INIT:
        BUZZER_SetLow();
        TurnstileState = SM_WAITING_CARD;
        break;
    case SM_WAITING_CARD:
        //  Check READERS
        validationMsg = 0;

        if (rfidAIsDataReady())
        {
            entryExit = 0; //Entry request
            cardNumber = rfidAGetCardNumberInt();
            msgType = MSG_ENTRY_REQUIRED;
            TurnstileState = SM_PREPARE_FRAME;
        }

        if (rfidBIsDataReady())
        {
            entryExit = 1; //Exit request
            cardNumber = rfidBGetCardNumberInt();
            msgType = MSG_EXIT_REQUIRED;
            TurnstileState = SM_PREPARE_FRAME;
        }
        break;
    case SM_PREPARE_FRAME:
        if (!entryExit)
        {
            dataBuffer[sendDataLen++] = DIRECTION_ENTRY | AppConfig.DeviceID;
        }
        else
        {
            dataBuffer[sendDataLen++] = DIRECTION_EXIT | AppConfig.DeviceID;
        }
        dataBuffer[sendDataLen++] = msgType;
        dataBuffer[sendDataLen++] = (uint8_t) cardNumber;
        dataBuffer[sendDataLen++] = (uint8_t) (cardNumber >> 8);
        dataBuffer[sendDataLen++] = (uint8_t) (cardNumber >> 16);

        if (validationMsg)
        {
            dataBuffer[sendDataLen++] = testStatus;
        }

        setFrame(dataBuffer, sendDataLen);

        Timer = TickGet();

        TurnstileState = SM_WAIT_SERVER_RESPONSE;
        break;
    case SM_WAIT_SERVER_RESPONSE:
        //  Process answer
        if (isServerDataReady())
        {
            getFrame(dataBuffer);

            devId = dataBuffer[0] & 0x7F;
            msgType = dataBuffer[1];
            validCard = dataBuffer[2];

            if ((devId == AppConfig.DeviceID) && (validCard == 1))
            {
                switch (msgType)
                {
                case MSG_ENTRY_REQUIRED:
                    testRequired = dataBuffer[3];
                    testTimeout = dataBuffer[4];

                    TurnstileState = SM_CHECKING_ESD;
                    break;
                case MSG_ENTRY_RESULT:
                    if (testStatus)
                    {
                        peopleCounter++;
                    }
                    else
                    {

                    }
                    TurnstileState = SM_RESTARTING;
                    break;
                case MSG_EXIT_REQUIRED:
                    testTimeout = AppConfig.ExitTimeout;
                    TurnstileState = SM_ACCESS_GRANTED;
                    break;
                case MSG_EXIT_RESULT:
                    if (testStatus)
                    {
                        if (peopleCounter)
                        {
                            peopleCounter--;
                        }
                    }
                    else
                    {

                    }
                    TurnstileState = SM_RESTARTING;
                    break;
                default:
                    break;
                }

                Timer = TickGet();
            }
            else
            {
                //  Card not valid
                TurnstileState = SM_RESTARTING;
            }
        }

        // Time out if too much time is spent in this state
        if (TickGet() - Timer > 5 * TICK_SECOND)
        {
            TurnstileState = SM_RESTARTING;
        }
        break;
    case SM_CHECKING_ESD:
        if (MSG_GetValue() == 0)
        {
            testStatus = 1;
            TurnstileState = SM_ACCESS_GRANTED;

            Timer = TickGet();
        }

        // Time out if too much time is spent in this state
        if (TickGet() - Timer > AppConfig.ESDCheckTimeout * TICK_SECOND)
        {
            TurnstileState = SM_ACCESS_DENIED;
        }
        break;
    case SM_ACCESS_DENIED:
        validationMsg = 1;
        testStatus = 0;
        msgType = MSG_ENTRY_RESULT;
        TurnstileState = SM_PREPARE_FRAME;
        break;
    case SM_ACCESS_GRANTED:
        BUZZER_SetHigh();

        if (!entryExit)
        {
            O6_SetHigh();
            __delay_ms(1000);
        }
        else
        {
            O5_SetHigh();
            __delay_ms(250);
        }

        BUZZER_SetLow();
        TurnstileState = SM_WAITING_PASS;
        break;
    case SM_WAITING_PASS:
        validationMsg = 1;

        if (!entryExit)
        {
            msgType = MSG_ENTRY_RESULT;
        }
        else
        {
            msgType = MSG_EXIT_RESULT;
        }

        if (ALARM_GetValue() == 0)
        {
            testStatus = 1;
            TurnstileState = SM_PREPARE_FRAME;
        }

        // Time out if too much time is spent in this state
        if (TickGet() - Timer > testTimeout * TICK_SECOND)
        {
            testStatus = 0;
            TurnstileState = SM_PREPARE_FRAME;
        }
        break;
    case SM_RESTARTING:
        O5_SetLow();
        O6_SetLow();
        TurnstileState = SM_WAITING_CARD;
        break;
    case SM_IDLE:
        break;
    }
}