
#include <OneWire.h>            //Librairie du bus OneWire
#include <DallasTemperature.h>  //Librairie du capteur
#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include "setup.h"


#define BLYNK_DEBUG // Optional, this enables lots of prints
#define BLYNK_PRINT Serial

/* GPIO */
#define GPIO_BP     0
#define GPIO_LED    13
#define GPIO_SWITCH 12
#define GPIO_TEMP   14

/* L'état de la led est inversé */
#define LED_HIGH    LOW 
#define LED_LOW     HIGH

/* Capteur température */
OneWire oneWire(GPIO_TEMP); 
DallasTemperature sensors(&oneWire); 
DeviceAddress sensorDeviceAddress;


/* Variables internes */
float   Temp_val ;
float   Temp_consigne = 0 ;

int     BP_val = LOW ;
int     Relay_val = LOW ;

/* Time */
unsigned long timeNow = 0;
unsigned long timeLast = 0;
int seconds = 0;
int minutes = 0;

int minutes_etape = 0 ;
int minutes_etape_conf = 0 ;
int minutes_etape_reste = 0 ;
int minutes_all = 0 ;

/* Parametres */
float ParamHysteresis = 0.5 ;
int ParamEmpatageTemperature = 0xFF ;       // La consigne doit etre au dessus
int ParamEmpatageMaintienTemperature = 0 ;  // La consigne doit etre en dessous
int ParamEmpatageMaintienDuree_min = 0 ;

int ParamEbullitionTemperature = 0xFF ;     // La consigne doit etre au dessus
int ParamEbullitionDuree_min = 0 ;

int ParamRefroidissementTemperature = 0 ;   // La consigne doit etre en dessous

int StartStop = 0 ; 

bool bEmpatageTempTropBasse = false  ;
bool bEmpatageTempOk = false  ;

bool bPremierDepassementConsigne = false ; 

/* Datas Blynk */
#define DATA_OUT_TEMP                 V0
#define DATA_OUT_CONSIGNE             V1
#define DATA_OUT_RELAY                V2
#define DATA_OUT_LCD                  V3
#define DATA_OUT_MINUTES_ALL          V4
#define DATA_OUT_MINUTES_ETAPE        V5
#define DATA_OUT_MINUTES_ETAPE_CONF   V6
#define DATA_OUT_MINUTES_ETAPE_RESTE  V7
#define DATA_OUT_TERMINAL             V8

#define DATA_IN_START_STOP            V10

#define DATA_IN_PARAM_HYSTERESIS                V20
#define DATA_IN_PARAM_EMPATAGE_TEMP             V21
#define DATA_IN_PARAM_EMPATAGE_MAINTIEN_TEMP    V22
#define DATA_IN_PARAM_EMPATAGE_MAINTIEN_DUREE   V23
#define DATA_IN_PARAM_EBULLITION_TEMP           V24
#define DATA_IN_PARAM_EBULLITION_DUREE          V25
#define DATA_IN_PARAM_REFROIDISSEMENT_TEMP      V26

BlynkTimer timerUpdate;

WidgetLED ledRelay(DATA_OUT_RELAY);
WidgetLCD lcd(DATA_OUT_LCD);
WidgetTerminal terminal(DATA_OUT_TERMINAL);


/* Machine état brassage */
typedef enum
{
    ETAPE_STOP=0,
    ETAPE_CONF,
    ETAPE_EMPATAGE_CHAUFFAGE,
    ETAPE_EMPATAGE_USER1,
    ETAPE_EMPATAGE,
    ETAPE_EMPATAGE_USER2,
    ETAPE_EBULLITION,
    ETAPE_EBULLITION_USER1,
    ETAPE_REFROIDISSEMENT,
    ETAPE_FIN
}tEtape ;

tEtape eEtapeCourante = ETAPE_STOP ;


void setup(void)
{
    Serial.begin(115200);
    Serial.println("Init ...");

    /* Configuration GPIO */
    pinMode(GPIO_LED, OUTPUT);
    pinMode(GPIO_SWITCH, OUTPUT);
    pinMode(GPIO_BP, INPUT);
    pinMode(GPIO_TEMP, INPUT);

    /* force relay */
    RelayOff();

    LedOn();
    
    sensors.begin();                                  //Activation des capteurs
    sensors.getAddress(sensorDeviceAddress, 0);       //Demande l'adresse du capteur à l'index 0 du bus
    sensors.setResolution(sensorDeviceAddress, 10);   //Résolutions possibles: 9,10,11,12 -> 10 = 0.25°C

    InitTime();

    Serial.println("Blynk init ...");
    Blynk.begin(BLYNK_TOKEN,WIFI_SSID,WIFI_PWD);
    Serial.println("Blynk init Ok");
  
    LedOff();    

    // Setup a function to be called every second
    timerUpdate.setInterval(1000L, BlynkUpdate);
    
    Serial.println("Init Ok");
}


void loop(void)
{
    //  Serial.println("******");
    MajTime();
    
    /* Lecure BP */
    BP_val = LectureBouton();
  
    /* Lecture température */
    Temp_val = LectureTemperature();
  
    /* Machine etat */
    Brassage();
  
    /* Update blynk */
    BlynkRun();
}


void Brassage(void)
{
    switch (eEtapeCourante)
    {
      case ETAPE_STOP :
        /* Raz */
        minutes_all = 0 ;
        minutes_etape = 0 ;
        minutes_etape_conf = 0 ;
        minutes_etape_reste = 0 ;

        RelayOff();
        
        Temp_consigne = 0 ;
        bPremierDepassementConsigne = false ;
        
        if (1 == StartStop)
        {
            eEtapeCourante = ETAPE_CONF ;
            BlynkLcd("Set Conf","+ BP to start");
        }
        break ;
        
      case ETAPE_CONF :
        if (HIGH == BP_val)
        {
            minutes_etape = 0 ;
            RelayOn();
            eEtapeCourante = ETAPE_EMPATAGE_CHAUFFAGE ;
            BlynkLcd("Empatage","Chauf. en cours ...");
        }
        break ;
        
      case ETAPE_EMPATAGE_CHAUFFAGE :
        Temp_consigne = ParamEmpatageTemperature;
        if (Temp_val >= (Temp_consigne + ParamHysteresis))
        {
            RelayOff();
            eEtapeCourante = ETAPE_EMPATAGE_USER1 ;
            
            BlynkLcd("Empatage Ok","BP to continue"); 
            
            terminal.print("Empatage - Durée chauffage eau : " );
            terminal.print(minutes_etape);
            terminal.println(" min");
            terminal.flush();
            
            BlynkNotification("Empatage : Fin chauffage eau");  
        } 
        break ;
      
      case ETAPE_EMPATAGE_USER1 :
        if (HIGH == BP_val)
        {
            /* Raz minutes */
            minutes_etape = 0 ;
            eEtapeCourante = ETAPE_EMPATAGE ;
            BlynkLcd("Empatage","En cours ...");
        }
        break ;
        
    case ETAPE_EMPATAGE :
        Temp_consigne = ParamEmpatageMaintienTemperature ;
        minutes_etape_conf = ParamEmpatageMaintienDuree_min ;
        
        if (Temp_val <= (Temp_consigne + ParamHysteresis))
        {
            if (false == bEmpatageTempTropBasse)
            {
                BlynkLcd("Temp. basse","Ajouter eau");
                BlynkNotification("Empatage : Température trop basse");
                bEmpatageTempTropBasse = true ;
                bEmpatageTempOk = false ;
            }
        }
        else
        {
            if (false == bEmpatageTempOk)
            {
                BlynkLcd("Empatage","En cours ...");
                bEmpatageTempOk = true ;
                bEmpatageTempTropBasse = false;
            }
        }
        
        if (minutes_etape >= minutes_etape_conf)
        {
              eEtapeCourante = ETAPE_EMPATAGE_USER2 ;

              BlynkLcd("Empatage Ok","BP to continue");

              terminal.print("Empatage - Durée : " );
              terminal.print(minutes_etape);
              terminal.println(" min");
              terminal.flush();

              BlynkNotification("Empatage : Fin");
        }
        break ;
      
    case ETAPE_EMPATAGE_USER2 :
        if (HIGH == BP_val)
        {
             /* Raz minutes */
              minutes_etape = 0 ;
              eEtapeCourante = ETAPE_EBULLITION ;
              BlynkLcd("Ebullition","Chauff. En cours");
        }
        break ;
        
    case ETAPE_EBULLITION :
        Temp_consigne = ParamEbullitionTemperature ;
        minutes_etape_conf = ParamEbullitionDuree_min ;
        /* Température >= consigne */
        if (Temp_val >= (Temp_consigne + ParamHysteresis))
        {
            RelayOff();

            if (false == bPremierDepassementConsigne)
            {
                bPremierDepassementConsigne = true ;
                terminal.print("Ebullition - 1er depassement durée : " );
                terminal.print(minutes_etape);
                terminal.println(" min");
                terminal.flush();
            }   
        }
        else if  (Temp_val <= (Temp_consigne - ParamHysteresis))
        {
            RelayOn();
        }
        else
        {}

        /* Durée Max */
        if (minutes_etape >= minutes_etape_conf)
        {
              RelayOff();
              eEtapeCourante = ETAPE_EBULLITION_USER1 ;

              BlynkLcd("Ebullition Ok","BP to continue");

              terminal.print("Ebullition - Durée : " );
              terminal.print(minutes_etape);
              terminal.println(" min");
              terminal.flush();
                            
              BlynkNotification("Ebullition terminée !!!!");
        }
        break ;

     case ETAPE_EBULLITION_USER1 :
        if (HIGH == BP_val)
        {
             /* Raz minutes */
              minutes_etape = 0 ;
              eEtapeCourante = ETAPE_REFROIDISSEMENT ;
              BlynkLcd("Refroidissement","En cours ...");
        }
        break ;

    case ETAPE_REFROIDISSEMENT :
        Temp_consigne = ParamRefroidissementTemperature ;
        if (Temp_val <= (Temp_consigne + ParamHysteresis))
        {
            eEtapeCourante = ETAPE_FIN ;
            BlynkLcd("Refroidissement","terminée !!!!");

            terminal.print("Refroidissement - Durée : " );
            terminal.print(minutes_etape);
            terminal.println(" min");
            terminal.flush();
            
            BlynkNotification("Refroidissement terminée !!!!");
        }
        break;
    
     case ETAPE_FIN :
        break ;
       
     default : 
      break ; 
    }

    if (0 == StartStop)
    {
        //Serial.println("STOP");
        eEtapeCourante = ETAPE_STOP ;
    }
}


/****************************************
* Température
*/

float LectureTemperature(void)
{
    sensors.requestTemperatures(); //Demande la température aux capteurs
    return sensors.getTempCByIndex(0); 
}


/****************************************
* Relay
*/

void RelayOn(void)
{
    digitalWrite(GPIO_SWITCH, HIGH);
    Relay_val = HIGH ;
}

void RelayOff(void)
{
    digitalWrite(GPIO_SWITCH, LOW);
    Relay_val = LOW ;
}


/****************************************
* LED
*/

void LedOn(void)
{
    digitalWrite(GPIO_LED,LED_HIGH);
}

void LedOff(void)
{
    digitalWrite(GPIO_LED,LED_LOW);
}

/****************************************
* Bouton Poussoir
*/

int LectureBouton(void)
{
    int val ;
    val = digitalRead(GPIO_BP);
    if(HIGH == val)
    {
        val = LOW  ;
        LedOff();
    }
    else
    {
        val = HIGH ;
        LedOn();
    }
    /*Serial.print("BP : ");
    Serial.println(val);*/
    return val ;
}


/****************************************
* Gestion Temp
*/

void InitTime(void)
{
    Serial.println("Init Time");
    timeLast =  millis()/1000;
}

void MajTime(void)
{
    timeNow = millis() / 1000; // the number of milliseconds that have passed since boot
    seconds = timeNow - timeLast;//the number of seconds that have passed since the last time 60 seconds was reached.
    if (seconds >= 60) 
    {
        timeLast = timeNow;
        minutes = minutes + 1;
        
        /* Maj variables */
        minutes_etape = minutes_etape + 1;
        minutes_all = minutes_all + 1;  
    }

    /* Maj reste étape */
    if((0 != minutes_etape_conf)&& (minutes_etape_conf > minutes_etape))
    {
        minutes_etape_reste = minutes_etape_conf - minutes_etape ;
    }

    /*Serial.print("Time : ");
    Serial.print(hours );
    Serial.print(":");
    Serial.print(minutes );
    Serial.print(":");
    Serial.println(seconds );*/
}


/****************************************
* BLYNK
*/

/* SYNC CONNECT */

bool isFirstConnect = true;

BLYNK_CONNECTED()
{
    if (true == isFirstConnect)
    {
        Serial.println("Blynk connect ...");
            
        /* Bouton sur stop à l'init */
        Blynk.virtualWrite(DATA_IN_START_STOP,0);
        Blynk.virtualWrite(DATA_OUT_MINUTES_ALL,0);
        Blynk.virtualWrite(DATA_OUT_MINUTES_ETAPE,0);
        Blynk.virtualWrite(DATA_OUT_MINUTES_ETAPE_CONF,"-");
        Blynk.virtualWrite(DATA_OUT_MINUTES_ETAPE_RESTE,"-");
        ledRelay.off();
        delay(100); 
        BlynkLcd("Appuyer sur start","pour commencer");
        
        Blynk.syncAll();
        Serial.println("Blynk connect Ok");
        isFirstConnect = false;
    }
}


void BlynkLcd(const char * pLigne1, const char * pLigne2)
{
    lcd.clear();
    if (NULL != pLigne1)
    {        
        lcd.print(0,0,pLigne1);
    }  
    if (NULL != pLigne2)
    {
        lcd.print(0,1,pLigne2);
    }
    /* force run */
    Blynk.run();
}


void BlynkNotification(const char * pMessage)
{
    if (NULL != pMessage)
    {
        /* force run before */
        Blynk.run();
        Blynk.notify(pMessage);
    }
}



void BlynkRun(void)
{
    /* Blynk run */
    Blynk.run();

    /* update timer */
    timerUpdate.run(); // Initiates BlynkTimer
}


void BlynkUpdate(void)
{
    Serial.println("Update Blynk");
    
    Blynk.virtualWrite(DATA_OUT_TEMP, Temp_val);
    Blynk.virtualWrite(DATA_OUT_CONSIGNE, Temp_consigne);
          
    Blynk.virtualWrite(DATA_OUT_MINUTES_ALL, minutes_all);
    Blynk.virtualWrite(DATA_OUT_MINUTES_ETAPE,minutes_etape);
    
    if (0 == minutes_etape_conf)
    {
        Blynk.virtualWrite(DATA_OUT_MINUTES_ETAPE_CONF, "-");
        Blynk.virtualWrite(DATA_OUT_MINUTES_ETAPE_RESTE,"-");
    }
    else
    {
        Blynk.virtualWrite(DATA_OUT_MINUTES_ETAPE_CONF, minutes_etape_conf);
        Blynk.virtualWrite(DATA_OUT_MINUTES_ETAPE_RESTE, minutes_etape_reste);
    }
    
    /* Relay Status */
    if (HIGH == Relay_val)
    {
        ledRelay.on();
    }
    else
    {
        ledRelay.off();
    }
}


/************************************************
 * BOUTON START/STOP
 */
BLYNK_WRITE(DATA_IN_START_STOP)
{
    StartStop = param.asInt(); 
    Serial.print("StartStop :");
    Serial.println(StartStop );
}

/************************************************
 * PARAMETRES
 */

/* Hysteresis */
BLYNK_WRITE(DATA_IN_PARAM_HYSTERESIS)
{
    ParamHysteresis = ((float)param.asInt()) / 10; 
    Serial.print("Hysteresis :");
    Serial.println(ParamHysteresis);
}


/* Empatage */
BLYNK_WRITE(DATA_IN_PARAM_EMPATAGE_TEMP)
{
    ParamEmpatageTemperature = param.asInt(); 
    Serial.print("Empatage Temp Chauffe :");
    Serial.println(ParamEmpatageTemperature);
}

BLYNK_WRITE(DATA_IN_PARAM_EMPATAGE_MAINTIEN_TEMP)
{
    ParamEmpatageMaintienTemperature = param.asInt(); 
    Serial.print("Param Empatage Temp Maintien :");
    Serial.println(ParamEmpatageMaintienTemperature);
}

BLYNK_WRITE(DATA_IN_PARAM_EMPATAGE_MAINTIEN_DUREE)
{
    ParamEmpatageMaintienDuree_min = param.asInt(); 
    Serial.print("Param Empatage Durée Maintien :");
    Serial.println(ParamEmpatageMaintienDuree_min);
}

/* Chauffe */
BLYNK_WRITE(DATA_IN_PARAM_EBULLITION_TEMP)
{
    ParamEbullitionTemperature = param.asInt(); 
    Serial.print("Param Ebullition Temp :");
    Serial.println(ParamEbullitionTemperature);
}

BLYNK_WRITE(DATA_IN_PARAM_EBULLITION_DUREE)
{
    ParamEbullitionDuree_min = param.asInt(); 
    Serial.print("Param Ebullition Durée :");
    Serial.println(ParamEbullitionDuree_min);
}

/* Refroidissement */
BLYNK_WRITE(DATA_IN_PARAM_REFROIDISSEMENT_TEMP)
{
    ParamRefroidissementTemperature = param.asInt(); 
    Serial.print("Param Refroidissement Temp :");
    Serial.println(ParamRefroidissementTemperature);
}

