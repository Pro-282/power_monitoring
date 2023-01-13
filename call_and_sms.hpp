#pragma once

#define TINY_GSM_MODEM_SIM800      // Modem is SIM800
#define TINY_GSM_RX_BUFFER   1024  // Set RX buffer to 1Kb

#include <Wire.h>
#include <TinyGsmClient.h>

// Set serial for debug console (to Serial Monitor, default speed 115200)
#define SerialMon Serial
// Set serial for AT commands (to SIM800 module)
#define SerialAT  Serial1

#define MODEM_RST             5
#define MODEM_PWRKEY          4
#define MODEM_POWER_ON       23
#define MODEM_TX             27
#define MODEM_RX             26

#define I2C_SDA              21
#define I2C_SCL              22
#define LED_GPIO             13
#define LED_ON               HIGH
#define LED_OFF              LOW

#define IP5306_ADDR          0x75
#define IP5306_REG_SYS_CTL0  0x00

// Set phone numbers, if you want to test SMS and Calls
// #define SMS_TARGET  "+380xxxxxxxxx" //todo: I'm to change this later
// #define CALL_TARGET "+380xxxxxxxxx"
std::string recipient_no;

TinyGsm modem(SerialAT);		//TinyGSM object instance

// setPowerBoostKeepOn
bool setup_PMU(bool en)
{
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.beginTransmission(IP5306_ADDR);
    Wire.write(IP5306_REG_SYS_CTL0);
    if (en) {
        Wire.write(0x37); // Set bit1: 1 enable 0 disable boost keep on
    } else {
        Wire.write(0x35); // 0x37 is default reg value
    }
    return Wire.endTransmission() == 0;
}

void setup_SIM800()
{
    // Start power management
    if (setup_PMU(true) == false) {
        Serial.println("Setting power error");
    }

#ifdef MODEM_RST
    // Keep reset high
    pinMode(MODEM_RST, OUTPUT);
    digitalWrite(MODEM_RST, HIGH);
#endif

    pinMode(MODEM_PWRKEY, OUTPUT);
    pinMode(MODEM_POWER_ON, OUTPUT);

    // Turn on the Modem power first
    digitalWrite(MODEM_POWER_ON, HIGH);

    // Pull down PWRKEY for more than 1 second according to manual requirements
    digitalWrite(MODEM_PWRKEY, HIGH);
    delay(100);
    digitalWrite(MODEM_PWRKEY, LOW);
    delay(1000);
    digitalWrite(MODEM_PWRKEY, HIGH);

    // Initialize the indicator as an output
    pinMode(LED_GPIO, OUTPUT);
    digitalWrite(LED_GPIO, LED_OFF);
}

void init_SIM800_serial()
{
    SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
    // delay(3000);
}

void init_SIM800()
{
	SerialMon.println("Initializing modem...");
  	modem.init();
}

void send_SMS( std::string SMS_target, std::string message )
{
	if( modem.sendSMS( String(SMS_target.c_str()), String(message.c_str()) ) ){
    SerialMon.println(message.c_str());
  }
  else{
    SerialMon.println("SMS failed to send");
  }
}

bool recieved_call( std::string *tel_no )
{
    char buffer[100];
    if( SerialAT.available() > 0 )
    {
        // Serial.println("I got here 1");
        // if(SerialAT.find("RING")){
        //     Serial.println("I got here 2.1");
        //     SerialAT.readBytes(buffer, sizeof(buffer));
        //     Serial.println("I'm here");
        // }
        // else return 0;
    // }
        SerialAT.readBytes(buffer, sizeof(buffer));
        std::string buff(buffer);
        if(buff.find("RING") != std::string::npos){
            size_t pos;
            Serial.println("I'm here");
            if(buff.find("\"+") != std::string::npos)
            {
                pos = buff.find("\"+");
                *tel_no = buff.substr(pos + 1, 14);
                return 1;
            }
        }
        else return 0;
    }
    else {
        Serial.println("noting in serial");
        return 0;
    }
}