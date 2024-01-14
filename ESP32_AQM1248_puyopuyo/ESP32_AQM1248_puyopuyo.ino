// **********************************************
// * ESP32_AQM1248_puyopuyo
// ***********************************************/
#include <EEPROM.h>
#include "AQM1248A.h"
#include "puyopuyo.h"

#define KEY_LEFT        32
#define KEY_RIGHT       14
#define KEY_UP          33
#define KEY_DOWN        27
#define KEY_A           12
#define KEY_B           22
#define KEY_C           16
#define KEY_D           19

#define KEY_START       13
#define KEY_SELECT      2
#define KEY_OP          17


#define KEY_LEFT_INDEX   0
#define KEY_RIGHT_INDEX  1
#define KEY_UP_INDEX     2
#define KEY_DOWN_INDEX   3
#define KEY_A_INDEX      4
#define KEY_B_INDEX      5
#define KEY_C_INDEX      6
#define KEY_D_INDEX      7
#define KEY_START_INDEX  8
#define KEY_SELECT_INDEX 9
#define KEY_OP_INDEX     10

#define PUSH_KEY        LOW

#define KEY_INTERVAL     2 // msec
#define KEY_MIN_INTERVAL_COUNT 20 // msec
#define TITLE_INTERVAL   50 // msec
#define DISPLAY_INTERVAL 20 // msec

bool key_data[6] = {false, false, false, false, false, false};
bool key_data_before[6] = {false, false, false, false, false, false};
int key_data_count[6] = {0, 0, 0, 0, 0, 0};

char v_buf[128][6];

int score = 0;

// ぷよぷよ関連変数
//#define PUYO_KIND 6
#define PUYO_KIND 4
#define PUYO_ERASE 6
#define PUYO_X_MAX 6
#define PUYO_Y_MAX 13
#define FIELD_X_MAX 8
#define FIELD_Y_MAX 14

//#define SPEED_UP_CNT_NUM 10
#define SPEED_UP_CNT_NUM 2
// #define SPEED_UP_RATIO   20
#define SPEED_UP_RATIO   5
#define SPEED_UP_MAX     10
#define PUYO_DOWN_CNT_DEFAULT 200
// #define PUYO_DOWN_CNT_DEFAULT 100
int speed_up_counter = 0;
int speed_lv = 1;
int puyo_down_count = PUYO_DOWN_CNT_DEFAULT;    // 200 * 10ms = 1000msec 
int field[FIELD_Y_MAX][FIELD_X_MAX];		// 画面データ
int cmb[FIELD_Y_MAX][FIELD_X_MAX];			// 結合チェック用
bool elist[30];			// 消すリスト

int hi_score = 0;

int kx1, ky1, kx2, ky2;			// 仮の座標
bool flag;						// 汎用フラグ
static int pnext1, pnext2;		// ネクストぷよ番号（０～４）
static int pno1, pno2;			// 現在のぷよ番号（０～４）
static int px1, py1, px2, py2;	// ぷよのＸ、Ｙ座標
static bool overFlag = false;	// ゲームオーバーフラグ
//static int downTime = 100;	// 1ブロック落下する時間(ms)
static int downCount;			// 落下時間までの猶予(ms)
static int rensa_count = 0;   // 連鎖カウント
static enum {					// 状態
	NEXT,						// ネクストぷよ出現
	NORMAL,						// 通常
	FALL,						// ぷよ落下
	ERASE1,						// ぷよ消し前
	ERASE2,						// ぷよ消し
} status = NEXT;

/*
Ticker ticker1;
bool  bReadyTicker = false;
*/
volatile int timeCounter0_key = 0;
volatile bool key_interval_flag = false;
volatile int timeCounter0_title = 0;
volatile bool title_interval_flag = false;
volatile int timeCounter0_display = 0;
volatile bool display_interval_flag = false;

volatile int timeCounter0_puyo_down_count = 0;
hw_timer_t *timer0 = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

SPISettings spiSettings = SPISettings(SPI_CLK, SPI_MSBFIRST, SPI_MODE1);

/*****************************************************************************
 *                          Interrupt Service Routin                         *
 *****************************************************************************/
void IRAM_ATTR onTimer0(){
  // Increment the counter and set the time of ISR
  portENTER_CRITICAL_ISR(&timerMux);

  timeCounter0_key++;
  if(timeCounter0_key > KEY_INTERVAL){
    timeCounter0_key = 0;
    key_interval_flag = true;
  }
  key_data_count[0]++;
  key_data_count[1]++;
  key_data_count[2]++;
  key_data_count[3]++;
  key_data_count[4]++;
  key_data_count[5]++;

  timeCounter0_title++;
  if(timeCounter0_title > TITLE_INTERVAL){
    timeCounter0_title = 0;
    title_interval_flag = true;
  }

  timeCounter0_display++;
  if(timeCounter0_display > DISPLAY_INTERVAL){
    timeCounter0_display = 0;
    display_interval_flag = true;
  }

  timeCounter0_puyo_down_count++;
  portEXIT_CRITICAL_ISR(&timerMux);
}

//データをEEPROMから読み込む。保存データが無い場合デフォルトにする。
int eeprom_load_data() {
    int data = 0;
    EEPROM.get(0, data);
    Serial.println("eeprom get");
    Serial.println(data);
    return data;
}

//EEPROMへの保存
void eeprom_save_data(int save_data) {
    //EEPROMに設定を保存する。
    EEPROM.put(0, save_data);
    EEPROM.commit();
    Serial.println("eeprom put and commit");
    Serial.println(save_data);
}

void setup()
{
    // Timer: interrupt time and event setting.
    timer0 = timerBegin(0, 80, true);
    // Attach onTimer function.
    timerAttachInterrupt(timer0, &onTimer0, true);
    // Set alarm to call onTimer function every second (value in microseconds).
    timerAlarmWrite(timer0, 10000, true);
    // Start an alarm
    timerAlarmEnable(timer0);

    Serial.begin(115200);
    Serial.println();

    EEPROM.begin(32); //サイズ

    pinMode(KEY_LEFT, INPUT_PULLUP);
    pinMode(KEY_RIGHT, INPUT_PULLUP);
    pinMode(KEY_UP, INPUT_PULLUP);
    pinMode(KEY_DOWN, INPUT_PULLUP);
    pinMode(KEY_A, INPUT_PULLUP);
    pinMode(KEY_B, INPUT_PULLUP);
//    pinMode(KEY_C, INPUT_PULLUP);
//    pinMode(KEY_D, INPUT_PULLUP);
//    pinMode(KEY_START, INPUT_PULLUP);
//    pinMode(KEY_SELECT, INPUT_PULLUP);
//    pinMode(KEY_OP, INPUT_PULLUP);

    SPI.begin();
    Init_LCD();
    LCD_CLS(0);

    LCD_Print_Str(0,0,"LCD OK",1);

    // 十字キー全押しで起動の場合は、ハイスコアを0初期化する
    key_interval_flag = true;
    Key_Input_Read();
    if(key_data[KEY_LEFT_INDEX] == LOW &&
       key_data[KEY_RIGHT_INDEX] == LOW &&
       key_data[KEY_UP_INDEX] == LOW &&
       key_data[KEY_DOWN_INDEX] == LOW){
       eeprom_save_data(0);
//       EEPROM.put(0, 0);
       LCD_Print_Str(4,60,"ﾊｲｽｺｱｦ",1);
       LCD_Print_Str(0,68,"ｼｮｷｶｼﾏｼﾀ",1);
    }

    LCD_WRITE();
    delay(1000);

    Serial.println("LCD_OK");
}

void loop()
{
    LCD_CLS(0);
    Game_Title();

    LCD_CLS(0);
    LCD_Print_Str(8,64,"GAME",1);
    LCD_Print_Str(8,72,"START",1);
    delay(1000);

    LCD_CLS(0);
    status = NEXT;
    randomSeed(analogRead(0));
    pnext1 = rand() % PUYO_KIND; pnext2 = rand() % PUYO_KIND;
    fieldCreate();
    score = 0;
    overFlag = false;
    puyo_down_count = PUYO_DOWN_CNT_DEFAULT;
    speed_lv = 1;
    rensa_count = 0;
    while(1){
        if(MainScreen() == false){
          LCD_Print_Str(4,64," GAME ",1);
          LCD_Print_Str(4,72,"OVER!!",1);
          LCD_WRITE();

          // EEPROMからハイスコアを読み出し
          // hi_score = eeprom_load_data();
          EEPROM.get(0, hi_score);

          if(score > hi_score){
//            eeprom_save_data(score);
            EEPROM.put(0, score);
            EEPROM.commit();
            LCD_Print_Str(4,90," ｵﾒﾃﾞﾄｳ ",1);
            LCD_Print_Str(0,98,"ﾊｲｽｺｱﾃﾞｽ",1);
            LCD_WRITE();
          }

          delay(3000);
          break;
        }
    }
}

void Init_LCD()
{
    pinMode(LCD_CS, OUTPUT);
    digitalWrite(LCD_CS,HIGH);

    pinMode(LCD_RS, OUTPUT);
    digitalWrite(LCD_RS,HIGH);

    pinMode(LCD_RSET, OUTPUT);
    digitalWrite(LCD_RSET,LOW);
    delay(500);
    digitalWrite(LCD_RSET,HIGH);

    digitalWrite(LCD_CS,LOW);
    digitalWrite(LCD_RS,LOW);

    // 液晶の初期化手順（メーカー推奨手順）
    SPI.transfer(0xAE);
    SPI.transfer(0xA0);
    SPI.transfer(0xC8);
    SPI.transfer(0xA3);

    // 内部レギュレータを順番にＯＮする
    SPI.transfer(0x2C); //power control 1
    delay(50);
    SPI.transfer(0x2E); //power control 2
    delay(50);
    SPI.transfer(0x2F); //power control 3

    // コントラスト設定
    SPI.transfer(0x23); //Vo voltage resistor ratio set
    SPI.transfer(0x81); //Electronic volume mode set
    SPI.transfer(0x1C); //Electronic volume value set

    // 表示設定
    SPI.transfer(0xA4); //display all point = normal（全点灯しない）
    SPI.transfer(0x40); //display start line = 0
    SPI.transfer(0xA6); //Display normal/revers = normal(白黒反転しない）
    SPI.transfer(0xAF); //Display = ON

    digitalWrite(LCD_CS,HIGH);
}

//----------------------------------------------------
//  画面へのデータの描画
//  int x  X positon   0 -> 48
//  int y  Y positon   0 -> 128
//  char *data  Data
//  int numlen  １行のデータサイズ(１行で何char数か)
//  int numline  データの行数
//----------------------------------------------------
void LCD_Print(int x, int y, char *data, int numlen, int numline)
{
    int b,c,d,e;
    char s;

    for(b=0; b<numline; b ++)
    {
        for(c=0; c<numlen; c ++)
        {
            s = data[b * numlen + c];
            for(d=0; d<8; d ++)
            {
                e=0;
                if(s & 0b10000000) e=1;
                LCD_PSET(x+(c*8)+d, y + b, e);
                s <<= 1;
            }
        }
    }
}

void LCD_CLS(char data)
{
    int a,b;

    digitalWrite(LCD_CS,LOW);
    for(b=0; b<6; b ++)
    {
        digitalWrite(LCD_RS,LOW);
        SPI.transfer(0xB0+b);
        SPI.transfer(0x10);
        SPI.transfer(0x00);

        digitalWrite(LCD_RS,HIGH);
        for(a=0; a<128; a++)
        {
            SPI.transfer(data);
            v_buf[a][b]=data;
        }
    }
    digitalWrite(LCD_CS,HIGH);
}

void LCD_vBUF_CLEAR()
{
    int a,b;

    for(b=0; b<6; b ++)
    {
        for(a=0; a<128; a++)
        {
            v_buf[a][b]=0;
        }
    }
}

void LCD_WRITE()
{
    int a,b;

    digitalWrite(LCD_CS,LOW);
    for(b=0; b<6; b ++)
    {
        digitalWrite(LCD_RS,LOW);
        SPI.transfer(0xB0+b);
        SPI.transfer(0x10);
        SPI.transfer(0x00);

        digitalWrite(LCD_RS,HIGH);
        for(a=0; a<128; a++)
        {
            SPI.transfer(v_buf[a][b]);
        }
    }
    digitalWrite(LCD_CS,HIGH);
}

//----------------------------------------------------
//  点の描画
//  int x_data  X positon   0 -> 128
//  int x_data  Y positon   0 -> 48
//  int cl      color 0: white  1:black
//----------------------------------------------------
void LCD_PSET(int x_data, int y_data, int cl)
{

    int a,b;
    char c;

//  y_data
    a=x_data >> 3; b= x_data & 0x07;
    c=0x1;
    while(b)
    {
        c <<= 1; b --;
    }

    if(cl) v_buf[127-y_data][a] |= c;
    else
    {
        c = ~c; v_buf[127-y_data][a] &= c;
    }
/*
    digitalWrite(LCD_CS,LOW);
    digitalWrite(LCD_RS,LOW);
    SPI.transfer(0xB0+a);
    c=(127-y_data) >> 4; c |= 0x10;
    SPI.transfer(c);
    c=(127-y_data) & 0xf;
    SPI.transfer(c);
    digitalWrite(LCD_RS,HIGH);
    SPI.transfer(v_buf[127-y_data][a]);
    digitalWrite(LCD_CS,HIGH);
*/
}

//----------------------------------------------------
//  Fontの描画
//  int x_data  X positon   0 -> 128
//  int y_data  Y positon   0 -> 48
//  char c_data Data
//  int cl      color 0: white  1:black
//----------------------------------------------------
void LCD_Print_C(int x_data, int y_data, char c_data, int cl)
{
    int a,b,c,d;
    char s;

    a = c_data - 0x20;
    for(b=0; b<5; b ++)
    {
        s=0x1;
        for(c=0; c<8; c ++)
        {
            d=0;
            if(Font[a][b] & s) d=1;
            if(cl == 0)
            {
                if(d) d=0;
                else d=1;
            }
            LCD_PSET(x_data,y_data + c,d);
            s <<= 1;
        }
        x_data ++;
    }
    for(c=0; c<8; c ++)
    {
        d=0;
        if(cl == 0) d=1;
        LCD_PSET(x_data,y_data + c,d);
    }
}

//----------------------------------------------------
//  Stringの描画
//  int x_data  X positon   0 -> 128
//  int y_data  Y positon   0 -> 48
//  char *c_data Data
//  int cl      color 0: white  1:black
//----------------------------------------------------
void LCD_Print_Str(int x_data, int y_data, char *c_data, int cl)
{
    int a;
    a = strlen(c_data);
    while(a)
    {
        if(*c_data == 0xef)
        {
            c_data += 2;
            a -= 2;
        }
        LCD_Print_C(x_data,y_data,*c_data,cl);
        a --; x_data += 6; c_data ++;
    }
}

//----------------------------------------------------
//   直線描画関数
//
//  int x0      start x
//  int y0      start y
//  int x1      end x
//  int y1      end y
//  int cl      color 0: white  1:black
//----------------------------------------------------
#define abs(a)  (((a)>0) ? (a) : -(a))
void LCD_LINE(int x0, int y0, int x1, int y1, int cl)
{
    int steep, t;
    int deltax, deltay, error;
    int x, y;
    int ystep;

    /// 差分の大きいほうを求める
    steep = (abs(y1 - y0) > abs(x1 - x0));
    /// ｘ、ｙの入れ替え
    if(steep)
    {
        t = x0; x0 = y0; y0 = t;
        t = x1; x1 = y1; y1 = t;
    }
    if(x0 > x1)
    {
        t = x0; x0 = x1; x1 = t;
        t = y0; y0 = y1; y1 = t;
    }
    deltax = x1 - x0;                       // 傾き計算
    deltay = abs(y1 - y0);
    error = 0;
    y = y0;
    /// 傾きでステップの正負を切り替え
    if(y0 < y1) ystep = 1; else ystep = -1;
    /// 直線を点で描画
    for(x=x0; x<x1+1; x++)
    {
        if(steep) LCD_PSET(y,x,cl); else LCD_PSET(x,y,cl);
        error += deltay;
        if((error << 1) >= deltax)
        {
            y += ystep;
            error -= deltax;
        }
    }
}

void Game_Title()
{
    // EEPROMからハイスコアを読み出し
//    hi_score = eeprom_load_data();
    EEPROM.get(0, hi_score);

    LCD_CLS(0);
    LCD_Print(0, 0, (char *)Puyo[4], 1, 8);
    LCD_Print(8, 0, (char *)Puyo[0], 1, 8);
    LCD_Print(16, 0, (char *)Puyo[1], 1, 8);
    LCD_Print(24, 0, (char *)Puyo[2], 1, 8);
    LCD_Print(32, 0, (char *)Puyo[5], 1, 8);
    LCD_Print(40, 0, (char *)Puyo[3], 1, 8);
    LCD_Print(0, 20, (char *)PuyoPuyoLogo, 6, 39);
    LCD_Print(0, 98, (char *)Puyo[4], 1, 8);
    LCD_Print(8, 98, (char *)Puyo[2], 1, 8);
    LCD_Print(16, 98, (char *)Puyo[3], 1, 8);
    LCD_Print(24, 98, (char *)Puyo[1], 1, 8);
    LCD_Print(32, 98, (char *)Puyo[0], 1, 8);
    LCD_Print(40, 98, (char *)Puyo[5], 1, 8);
    LCD_Print_Str(4,110,"HISCORE",1);
    LCD_print_score(9, 119, score);

    bool title_menu_alt_flag = true;
    while(1)
    {
        if(title_interval_flag){
            title_interval_flag = false;
            if(title_menu_alt_flag){
                title_menu_alt_flag = false;
                LCD_Print_Str(8,70," PUSH ",1);
                LCD_Print_Str(8,78,"START!",1);
            } else {
                title_menu_alt_flag = true;
                LCD_Print_Str(8,70," PUSH ",0);
                LCD_Print_Str(8,78,"START!",0);
            }
            LCD_WRITE();
        }

        if(Key_Input_Read()){
//            if(key_data[KEY_START_INDEX] == LOW){
            if(key_data[KEY_B_INDEX] == LOW){
                break;
            }

            if(hi_score > 100000){
              hi_score = 100000;
            }
            if(hi_score < -1){
              hi_score = -1;
            }
            LCD_print_score(9, 119, hi_score);
        }
    }
}

void LCD_print_score(int x, int y, int score_data)
{
    char buf[8];

    if(0 <= score_data && score_data <= 99999){
        sprintf(buf, "%5d", score_data);
    } else {
        sprintf(buf, "ERROR");
    } 
    LCD_Print_Str(x,y,buf,1);
}

void LCD_print_num(int x, int y, int num_data)
{
    char buf[8];
    sprintf(buf, "%d", num_data);
    LCD_Print_Str(x,y,buf,1);
}

void LCD_print_puyo()
{
    int x,y;
  	for (y = 2; y < FIELD_Y_MAX-1; y++) {
  		for (x = 1; x < FIELD_X_MAX-1; x++) {
  			if (field[y][x] != -1) {
          LCD_Print((x-1)*8, ((y-2)*8)+23, (char *)Puyo[field[y][x]], 1, 8);
  			}
  		}
  	}
}

bool Key_Input_Read(){
    int i;
    bool key_temp;
    // for(i=0; i<6; i ++)
    // {
    //     key_data[i] = HIGH;
    // }
    if(key_interval_flag) {
        key_interval_flag = false;
        key_temp = digitalRead(KEY_LEFT);
        if(key_data_before[KEY_LEFT_INDEX] == PUSH_KEY && key_temp == PUSH_KEY && key_data_count[KEY_LEFT_INDEX] < KEY_MIN_INTERVAL_COUNT) {
            key_data[KEY_LEFT_INDEX] = HIGH;
        } else {
            key_data[KEY_LEFT_INDEX] = key_temp;
            key_data_before[KEY_LEFT_INDEX] = key_temp;
            key_data_count[KEY_LEFT_INDEX] = 0;
        }

        key_temp = digitalRead(KEY_RIGHT);
        if(key_data_before[KEY_RIGHT_INDEX] == PUSH_KEY && key_temp == PUSH_KEY && key_data_count[KEY_RIGHT_INDEX] < KEY_MIN_INTERVAL_COUNT) {
            key_data[KEY_RIGHT_INDEX] = HIGH;
        } else {
            key_data[KEY_RIGHT_INDEX] = key_temp;
            key_data_before[KEY_RIGHT_INDEX] = key_temp;
            key_data_count[KEY_RIGHT_INDEX] = 0;
        }

        key_temp = digitalRead(KEY_UP);
        if(key_data_before[KEY_UP_INDEX] == PUSH_KEY && key_temp == PUSH_KEY && key_data_count[KEY_UP_INDEX] < KEY_MIN_INTERVAL_COUNT) {
            key_data[KEY_UP_INDEX] = HIGH;
        } else {
            key_data[KEY_UP_INDEX] = key_temp;
            key_data_before[KEY_UP_INDEX] = key_temp;
            key_data_count[KEY_UP_INDEX] = 0;
        }

        key_temp = digitalRead(KEY_DOWN);
        if(key_data_before[KEY_DOWN_INDEX] == PUSH_KEY && key_temp == PUSH_KEY && key_data_count[KEY_DOWN_INDEX] < KEY_MIN_INTERVAL_COUNT) {
            key_data[KEY_DOWN_INDEX] = HIGH;
        } else {
            key_data[KEY_DOWN_INDEX] = key_temp;
            key_data_before[KEY_DOWN_INDEX] = key_temp;
            key_data_count[KEY_DOWN_INDEX] = 0;
        }

        key_temp = digitalRead(KEY_A);
        if(key_data_before[KEY_A_INDEX] == PUSH_KEY && key_temp == PUSH_KEY && key_data_count[KEY_A_INDEX] < KEY_MIN_INTERVAL_COUNT) {
            key_data[KEY_A_INDEX] = HIGH;
        } else {
            key_data[KEY_A_INDEX] = key_temp;
            key_data_before[KEY_A_INDEX] = key_temp;
            key_data_count[KEY_A_INDEX] = 0;
        }

        key_temp = digitalRead(KEY_B);
        if(key_data_before[KEY_B_INDEX] == PUSH_KEY && key_temp == PUSH_KEY && key_data_count[KEY_B_INDEX] < KEY_MIN_INTERVAL_COUNT) {
            key_data[KEY_B_INDEX] = HIGH;
        } else {
            key_data[KEY_B_INDEX] = key_temp;
            key_data_before[KEY_B_INDEX] = key_temp;
            key_data_count[KEY_B_INDEX] = 0;
        }

//        key_data[KEY_C_INDEX] = digitalRead(KEY_C);
//        key_data[KEY_D_INDEX] = digitalRead(KEY_D);
        // key_temp = digitalRead(KEY_D);
        // if(key_data_before[KEY_D_INDEX] == PUSH_KEY && key_temp == PUSH_KEY && key_data_count[KEY_D_INDEX] < KEY_MIN_INTERVAL_COUNT) {
        //     key_data[KEY_D_INDEX] = HIGH;
        // } else {
        //     key_data[KEY_D_INDEX] = key_temp;
        //     key_data_before[KEY_D_INDEX] = key_temp;
        //     key_data_count[KEY_D_INDEX] = 0;
        // }

//        key_data[KEY_START_INDEX] = digitalRead(KEY_START);
//        key_data[KEY_SELECT_INDEX] = digitalRead(KEY_SELECT);
//        key_data[KEY_OP_INDEX] = digitalRead(KEY_OP);
        
        LCD_WRITE();
        return true;
    }
    return false;
}


///////////////////////////////////////////////////////////////////////////////
//	ウインドウ生成関数

void fieldCreate(void)
{
	// フィールドをクリア
	for (int y = 0; y < FIELD_Y_MAX; y++) {
		for (int x = 0; x < FIELD_X_MAX; x++) {
			if (x == 0 || x == FIELD_X_MAX-1 || y == FIELD_Y_MAX-1) field[y][x] = PUYO_ERASE;
			else field[y][x] = -1;
		}
	}
}

///////////////////////////////////////////////////////////////////////////////
//	メイン画面

// ぷよ結合チェック関数（再帰）
// 引数 x, y:調べるぷよ座標 pno:ぷよ番号 cno:結合番号
int CheckCombine(int x, int y, int pno, int cno)
{
	if (field[y][x] != pno || cmb[y][x] != 0) return 0;
	int ret = 1;
	cmb[y][x] = cno;
	if (y > 0) ret += CheckCombine(x, y - 1, pno, cno);		// 上
	if (x < (FIELD_X_MAX-1)) ret += CheckCombine(x + 1, y, pno, cno);				// 右
	if (y < FIELD_Y_MAX) ret += CheckCombine(x, y + 1, pno, cno);				// 下
	if (x > 1) ret += CheckCombine(x - 1, y, pno, cno);				// 左
	return ret;
}

bool MainScreen(void)
{
	int i, x, y;
  bool all_erase_flag = true;


	switch (status) {
	// ネクストぷよ出現
	case NEXT:
		px1 = 3; py1 = 1; px2 = 3; py2 = 0;
		pno1 = pnext1; pno2 = pnext2;
		pnext1 = rand() % PUYO_KIND; pnext2 = rand() % PUYO_KIND;

    // 難易度調整（落下速度増加）
    speed_up_counter++;
    if(speed_up_counter > SPEED_UP_CNT_NUM){
      speed_lv++;
      speed_up_counter = 0;
      puyo_down_count -= SPEED_UP_RATIO;
      if(puyo_down_count <= SPEED_UP_MAX){ puyo_down_count = SPEED_UP_MAX;}
    }

		status = NORMAL;
		break;
	// 通常
	case NORMAL:
		kx1 = px1; ky1 = py1;
		kx2 = px2; ky2 = py2;
		flag = false;
    if(Key_Input_Read()){
				if (key_data[KEY_A_INDEX] == PUSH_KEY) {
					if (kx2 > kx1) {kx2 = kx1; ky2 = ky1 + 1;}
					else if (kx2 < kx1) {kx2 = kx1; ky2 = ky1 - 1;}
					else if (ky2 > ky1) {ky2 = ky1; kx2 = kx1 - 1;}
					else {ky2 = ky1; kx2 = kx1 + 1;}
          if(kx1 < 1 || kx2 < 1) { kx1++; kx2++;}
          if(kx1 >= FIELD_X_MAX-1 || kx2 >= FIELD_X_MAX-1) { kx1--; kx2--;}
				} else if (key_data[KEY_B_INDEX] == PUSH_KEY) {
//				if (key_data[KEY_B_INDEX] == PUSH_KEY) {
					if (kx2 > kx1) {kx2 = kx1; ky2 = ky1 - 1;}
					else if (kx2 < kx1) {kx2 = kx1; ky2 = ky1 + 1;}
					else if (ky2 > ky1) {ky2 = ky1; kx2 = kx1 + 1;}
					else {ky2 = ky1; kx2 = kx1 - 1;}
          if(kx1 < 1 || kx2 < 1) { kx1++; kx2++;}
          if(kx1 >= FIELD_X_MAX-1 || kx2 >= FIELD_X_MAX-1) { kx1--; kx2--;}
				} else if (key_data[KEY_DOWN_INDEX] == PUSH_KEY) {
					ky1++; ky2++; flag = true;
				} else if (key_data[KEY_LEFT_INDEX] == PUSH_KEY) {
					kx1--; kx2--;
				} else if (key_data[KEY_RIGHT_INDEX] == PUSH_KEY) {
					kx1++; kx2++;
				}
    }    
    if (timeCounter0_puyo_down_count > puyo_down_count) {
      timeCounter0_puyo_down_count = 0;
			ky1++; ky2++; flag = true;
		}
		if (field[ky1][kx1] == -1 && field[ky2][kx2] == -1) {
			px1 = kx1; py1 = ky1;
			px2 = kx2; py2 = ky2;
		} else if (flag) {
			field[py1][px1] = pno1;
			field[py2][px2] = pno2;
			status = FALL;
		}
		break;
	// ぷよ落下
	case FALL:
		delay(200);
		flag = false;
		for (y = FIELD_Y_MAX-2; y >= 0; y--) {
			for (x = 1; x < FIELD_X_MAX-1; x++) {
				if (field[y][x] != -1 && field[y + 1][x] == -1) {
					field[y + 1][x] = field[y][x];
					field[y][x] = -1;
					flag = true;
				}
			}
		}
		if (flag == false) status = ERASE1;
		break;
	// ぷよ消し前
	case ERASE1:
		// ぷよ結合チェック
		flag = false;
		for (y = 0; y < FIELD_Y_MAX; y++) for (x = 0; x < FIELD_X_MAX; x++) cmb[y][x] = 0;
		for (i = 0; i < 30; i++) elist[i] = false;
		for (y = FIELD_Y_MAX - 2, i = 0; y >= 0; y--) {
			for (x = 1; x < FIELD_X_MAX-1; x++) {
				if (cmb[y][x] == 0 && field[y][x] != -1) {
					i++;
					int ret = CheckCombine(x, y, field[y][x], i);
					if (ret >= 4) {
						flag = true;
						elist[i] = true;
						score += ret * 10;
					}
				}
			}
		}
		if (flag) {
			// 結合ぷよがある場合は消しぷよと入れ替え
			for (y = FIELD_Y_MAX-2; y >= 0; y--) {
				for (x = 1; x < FIELD_Y_MAX-1; x++) {
					if (elist[cmb[y][x]]) field[y][x] = PUYO_ERASE;
				}
			}
      rensa_count++;
			status = ERASE2;
		} else {
			// 結合ぷよがない場合はゲームオーバーチェックして次へ
			for (y = 0; y < 2; y++) {
				for (x = 1; x < FIELD_X_MAX-1; x++) {
					if (field[y][x] != -1) overFlag = true;
				}
			}
      rensa_count = 0;
			status = NEXT;
		}
    LCD_print_puyo();
    LCD_WRITE();
		break;
	// ぷよ消し
	case ERASE2:
		delay(500);
    all_erase_flag = true;
		for (y = FIELD_Y_MAX-2; y >= 0; y--) {
			for (x = 1; x < FIELD_X_MAX-1; x++) {
				if (field[y][x] == PUYO_ERASE) {
          field[y][x]= -1;
        }
        if (field[y][x] != -1) {
          all_erase_flag = false;
        } 
			}
		}

    if(rensa_count > 1) {
      LCD_Print_Str(0,82,"        ",1);
      LCD_Print_Str(0,90," ",1);
      LCD_print_num(6,90,rensa_count);
      LCD_Print_Str(12,90,"ﾚﾝｻ!  ",1);
      LCD_Print_Str(0,98," +",1);
      LCD_print_num(12,98,rensa_count*50);
      LCD_Print_Str(36,98,"ﾃﾝ ",1);
      LCD_Print_Str(0,106,"        ",1);
      score += rensa_count*50;
      LCD_WRITE();
      delay(1000);
    }

    if(all_erase_flag) {
      LCD_Print_Str(0,82,"        ",1);
      LCD_Print_Str(0,90," ｾﾞﾝｹｼ  ",1);
      LCD_Print_Str(0,98," +200ﾃﾝ ",1);
      LCD_Print_Str(0,106,"        ",1);
      score += 200;
      LCD_WRITE();
      delay(1000);
    }
		status = FALL;
		break;
	}
	// 画面表示
  if(display_interval_flag){
    display_interval_flag = false;
//    LCD_CLS(0);
    LCD_vBUF_CLEAR();
    LCD_print_puyo();
  	// for (y = 2; y < FIELD_Y_MAX-1; y++) {
  	// 	for (x = 1; x < FIELD_X_MAX-1; x++) {
  	// 		if (field[y][x] != -1) {
    //       LCD_Print((x-1)*8, ((y-2)*8)+23, (char *)Puyo[field[y][x]], 1, 8);
  	// 		}
  	// 	}
  	// }
  	// 現在のぷよ表示
  	if (status == NORMAL) {
        if(py1 >= 2){LCD_Print((px1-1)*8, ((py1-2)*8)+23, (char *)Puyo[pno1], 1, 8);}
        if(py2 >= 2){LCD_Print((px2-1)*8, ((py2-2)*8)+23, (char *)Puyo[pno2], 1, 8);}
  	}
  	// ネクストぷよ表示
    LCD_Print(30, 13, (char *)Puyo[pnext1], 1, 8);
    LCD_Print(30+8, 13, (char *)Puyo[pnext2], 1, 8);

    // 各種表示
    LCD_Print_Str(2, 2,"SC",1);
    LCD_print_score(17, 2, score);
    LCD_Print_Str(2, 13,"NEXT",1);
    LCD_Print_Str(2, 112,"ｽﾋﾟｰﾄﾞ",1);
    LCD_print_num(36, 112,speed_lv);
    LCD_Print_Str(2, 120,"HS",1);
    LCD_print_score(17, 120,hi_score);
    LCD_LINE(0, 0, 47, 0, 1);
    LCD_LINE(0, 0, 0, 22, 1);
    LCD_LINE(47, 0, 47, 22, 1);
    LCD_LINE(0, 10, 47, 10, 1);
    LCD_LINE(0, 22, 47, 22, 1);
    LCD_LINE(0, 110, 47, 110, 1);
    LCD_LINE(0, 110, 0, 127, 1);
    LCD_LINE(47,110, 47,127, 1);
    LCD_LINE(0, 127, 47, 127, 1);
    LCD_WRITE();
  }

	// ゲームオーバー処理
	if (overFlag) {
    return false;
	}

  return true;
}