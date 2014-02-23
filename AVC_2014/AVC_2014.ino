/* Minuteman / Roadrunner competition code - creation of the multi-file branch

Pin Assignments:

A0 - Analog input from gyro
A1 - analog input for temp, from gyro
A2 - NC
A3 - NC
A4 - SDA
A5 - SCL
D0 - RX
D1 - TX
D2 - NC (normally Ch1 in)
D3 - Steering input (connected to Ch2 in on board)
D4 - MUX enble input (input only. manual if low, auto if high)
D5 - Mode input (switched by Ch3)
D6 - Toggle
D7 - NC
D8 - NC
D9 - NC (internally connected to MUX 1)  **consider connecting to MUX 3
D10 - Steering contorl out (internally connected to MUX 2
D11 - ESC control out (connect to MUX 3)
D12 - LED status
D13 - LED status

FULLY AUTONOMOUS MODE - MOVE THROTTLE INPUT TO 3 AND THROTTLE OUTPUT TO 3
STEERING AUTONOMOUS MODE - MOVE THROLLER INPUT TO 4 AND THROTTLE OUTPUT TO 4

*/

/*to do
x1. move the gyro to another file, make all values local, then static, then global
2. move the steering to another file
3. move the servo functions to another file
4. move the menu to another file
etc.
*/


//#INCLUDE FILES
/* the arduino ide is honky...
see: http://jamesreubenknowles.com/including-libraries-in-the-arduino-ide-1625
Any Arduino libraries that are needed by the files outside of the sketch (.ino) file must also be listed in the sketch file itself.
The sketch is parsed for include files. The sketch, all included header files, and the corresponding source files, are copied to another directory for compiling. From that directory, library-based include files are NOT available unless they are included in the sketch and copied to the build directory.
*/
#include "DECLARATIONS.h"
#include <Servo.h>
#include <EEPROM.h>
#include <Wire.h>
#include <I2Cdev.h>
#include <MPU6050.h>


//INTERNAL VARIABLES
//these are used for setting and clearing bits in special control registers on ATmega
volatile boolean gyro_flag = false;
bool manual, automatic, aux=false, running=false, first=true;
volatile byte clicks = 0;
unsigned int click_calibration_counter = 0;
long time=0;
long count, proximity, previous_proximity=50;
double x_wp = 0, y_wp = 0;
double angle_diff, angle_last, angle_target, x=0, y=0;
int steer_limm = 300, steer_us;
long speed_cur=0, speed_new=0, speed_old=0;
const byte InterruptPin = 2 ;		//interrupt on digital pin 2
double cross_product=0, target_x=0, target_y=0;


//EXTERNAL VARIABLES
extern long accum; //this is ONLY used to reset the 0 the gyro angle for real (setting angle to 0 does nothing!!! (never forget last year's debacle))
extern double angle;
extern byte wpr_count;
extern position_structure waypoint;


//OBJECT DECLARATIONS
Servo steering, esc;


//PROGRAM FUNCTIONS
void encoder_interrupt(){
    clicks++;
	return ;
}

void navigate(){
	calculate_speed();
	cal_steer_lim();
	update_position();
	update_cross_product();
	update_steering();
	update_waypoint();
	get_mode();
	if(automatic) steering.writeMicroseconds(steer_us);
	if(automatic) speed();
	
	return ;
}

void calculate_speed(){
    speed_new = micros();
    speed_cur = speed_new - speed_old;
    speed_old = speed_new;
	
	return ;
}

void cal_steer_lim(){
	steer_limm = (int)map(speed_cur, L1, L2, L3, L4);
	if(steer_limm > L4) steer_limm = L4;
	
	return ;
}

void update_position(){
	//calculate position
	x += sin(angle);
	y += cos(angle);
	angle_target = atan2((x_wp - x),(y_wp - y));
	double temp = pow((x_wp-x),2);
	temp += pow((y_wp-y),2);
	proximity = sqrt(temp);
	//proximity = sqrt(pow((x_wp - x),2) + pow((y_wp - y),2));	
	return ;
}

void update_cross_product(){
	//calculates the car's current vector
	double current_x = x_wp - x;
	double current_y = y_wp - y;

	//determines magnitude of each vector
	double mag_target = sqrt(target_x*target_x + target_y*target_y);
	if(mag_target == 0.0) mag_target = 0.0001;
	double mag_current = sqrt(current_x*current_x + current_y*current_y);
	if(mag_current == 0.0) mag_current = 0.0001;
	
	//make the current and target vectors into unit vectors
	target_x = target_x / mag_target;
	target_y = target_y / mag_target;
	current_x = current_x / mag_current;
	current_y = current_y / mag_current;
	
	//the actual cross product calculation
	cross_product = -(target_x*current_y - current_x*target_y);
	
	return ;
}

void update_steering(){
	// calculate and write angles for steering
	angle_diff = angle_target - angle;
	if(angle_diff < -3.14159) angle_diff += 3.14159*2;   //if angle is less than 180 deg, then add 360 deg
	if(angle_diff > 3.14159) angle_diff -= 3.14159*2;	//if angle is greater than 180 deg, then subtract 360
	// now, we have an angle as -180 < angle_diff < 180.
	// steer_us = angle_diff/(3.14159*2.0)*STEER_GAIN;
	steer_us = angle_diff/(3.14159*2.0)*STEER_GAIN + cross_product*CP_GAIN;	//cross product gain added  here so that the steering is still limited
	if(steer_us < (0-steer_limm)) steer_us = 0-steer_limm;
	if(steer_us > steer_limm) steer_us = steer_limm;
	steer_us += STEER_ADJUST;  //adjusts steering so that it will go in a straight line
	return ;
}

void update_waypoint(){
	//waypoint acceptance and move to next waypoint
	if(proximity < (WAYPOINT_ACCEPT/CLICK_INCHES)){
		wpr_count++;
		EEPROM_readAnything(wpr_count*WP_SIZE, waypoint);
		x_wp = waypoint.x;
		y_wp = waypoint.y;
		if (((int)x_wp == 0) && ((int)y_wp == 0)) end_run(); // 0,0 is interpreted as the final waypoint. end run.
		Serial.print("read WP #");
		Serial.print(wpr_count);
		Serial.print(": ");
		Serial.print(x_wp*CLICK_INCHES);
		Serial.print(" , ");
		Serial.println(y_wp*CLICK_INCHES);
		double temp = pow((x_wp-x),2);
		temp += pow((y_wp-y),2);
		proximity = sqrt(temp);
		//proximity = sqrt(pow((x_wp - x),2) + pow((y_wp - y),2));	
		previous_proximity = proximity;

		//sets up the planned cross product target vectors
		target_x = x_wp - target_x;
		target_y = y_wp - target_y;
	}
	
	return ;
}

void print_coordinates(){ //print target, location, etc.
	Serial.print("(x,y): ");
	Serial.print(x*CLICK_INCHES);
	Serial.print(" , ");
	Serial.print(y*CLICK_INCHES);	Serial.print("   trgt: ");
	Serial.print(x_wp*CLICK_INCHES);
	Serial.print(" , ");
	Serial.println(y_wp*CLICK_INCHES);
	// Serial.print("  crnt spd: ");
	// Serial.println(speed_cur);
	// Serial.print("\tagl tgt: ");
	// Serial.print(angle_target);
	// Serial.print("\tagl diff: ");
	// Serial.print(angle_diff);
	// Serial.print("\tprox: ");
	// Serial.print(proximity*CLICK_INCHES);
	// Serial.print("\tlim: ");
	// Serial.print(steer_limm);
	// Serial.print("\tsteer: ");
	// Serial.print(steer_us);
	// Serial.print("\tspeed: ");
	// Serial.println(speed_cur);
	// Serial.print("\tFree Memory = ");
	// Serial.println(freeMemory());

	return ;
}

void speed(){
	running = true;			// make sure running is updated.

	if((previous_proximity - proximity) <= (P1/CLICK_INCHES)) esc.writeMicroseconds(S2); //allow car to line up with the next point
	else if(proximity < (P2/CLICK_INCHES)) esc.writeMicroseconds(S2); //ensure that a waypoint can be accepted
	else if(proximity >= (P2/CLICK_INCHES) && proximity < (P3/CLICK_INCHES)){ //slow way down  50-200 works well, 50-300 is more conservative for higher speeds
		if(speed_cur < BREAKING_SPEED)  esc.writeMicroseconds(SB);  // less than 8000 means high speed, apply brakes
		else esc.writeMicroseconds(S3);  //once speed is low enough, resume normal slow-down
	}
	else if(proximity >= (P3/CLICK_INCHES)) esc.writeMicroseconds(S4); //go wide open 200 works well for me. 

	return ;
}

void get_mode(){
	if(!digitalRead(TMISO)){
		manual = true;
		automatic = false;
		aux = false;
    }
    else if(!digitalRead(MODE)){
		manual = false;
		automatic = false;
		aux = true;
    }
    else {
		manual = false;
		automatic = true;
		aux = false;
    }

	return ;	
}

void steering_calibration(){
	Serial.println();
	angle_target = 0.0;
		
	steering.attach(10);
	steering.writeMicroseconds(STEER_ADJUST);
	delay(500);
	setup_mpu6050();
	calculate_null();

	Serial.println("set controller to automatic");
	get_mode();
	while(!automatic) get_mode();
	
	while(automatic){
		read_FIFO();
		
		update_steering();
		steering.writeMicroseconds(steer_us);
		
		if((millis()-time)>200){
			Serial.print("angle: ");
			Serial.print(angle,5);
			Serial.print("\tsteering ms: ");
			Serial.println(steer_us);
			time = millis();
		}
		get_mode();
	}

	steering.detach();

	return ;
}

void click_calibration(){
	Serial.println();
	click_calibration_counter = 0;
	pinMode(InterruptPin, INPUT);
	attachInterrupt(0, click_calibration_increment, CHANGE);	//interrupt 0 is on digital pin 2
	get_mode();
	while(manual){
		if((millis()-time)>1000){
			Serial.println(click_calibration_counter);
			time = millis();
		}
		get_mode();
	}
	
	detachInterrupt(0);
	Serial.println();
	Serial.print("Total clicks: ");
	Serial.println(click_calibration_counter);
	Serial.println();
	return ;
}

void click_calibration_increment(){
    click_calibration_counter++;
	return ;
}

void menu_choices(){
	Serial.println();
	Serial.println("Main Menu");
	Serial.println("----------");
	Serial.println("a = watch angle");
	Serial.println("d = display waypoints");
	Serial.println("e = edit waypoint");
	Serial.println("f = click calibration");
	Serial.println("i = import header waypoint values");
	Serial.println("l = gyro calibration");
	Serial.println("m = free memory");
	Serial.println("s = steering calibration");
	Serial.println("w = watch gyro");
	Serial.println("x = exit. start setup routine for the race");
	Serial.println();
	return ;
}

void main_menu(){
	int loop = 1;
	menu_choices();
	Serial.flush();
	get_mode();
	while((loop) && (manual)){
		get_mode();
		if(Serial.available() > 0){
	 		switch(Serial.read()){
				case 'a':
					watch_angle();
					menu_choices();
					break;
				case 'd':
					display_waypoints();
					menu_choices();
					break;
				case 'e':
					edit_waypoint();
					menu_choices();
					break;
				case 'f':
					click_calibration();
					menu_choices();
					break;
				case 'i':
					import_waypoints();
					menu_choices();
					break;
				case 'l':
					gyro_calibration();
					menu_choices();
					break;
				case 'm':
					Serial.println();
					Serial.println();
					Serial.print("available memory: ");
					//Serial.println(freeMemory());
					Serial.println();
					Serial.println();
					menu_choices();
					break;
				case 's':
					steering_calibration();
					menu_choices();
					break;
				case 'w':
					watch_gyro();
					menu_choices();
					break;
				case 'x':
					Serial.println();
					Serial.println();
					Serial.println("Setting up for the race");
					loop = 0;
					break;
				default:
					Serial.println("invalid entry. try again.");
					menu_choices();
					break;
			}
		delay(500);
		get_mode();
		}
	}
	return ;
}

void setup(){
	//Pin assignments:
	pinMode(TMISO, INPUT);
	pinMode(MODE, INPUT);
	pinMode(TOGGLE, INPUT);
	digitalWrite(TOGGLE, HIGH);
	pinMode(12, OUTPUT);
	digitalWrite(12, LOW); 

	Wire.begin();

	Serial.begin(115200);
	Serial.setTimeout(100000);
	Serial.println(CAR_NAME);
	Serial.println();
	
	get_mode();
	main_menu();
	delay(500);	

	if(digitalRead(TOGGLE)){
		Serial.println("FLIP SWITCH TO CONTINUE");
		Serial.println();
	}
	while(digitalRead(TOGGLE)) get_mode();		//waits until the switch is flipped to start the race
	delay(2000);

	setup_mpu6050();
	calculate_null();

	pinMode(InterruptPin, INPUT);	 
	attachInterrupt(0, encoder_interrupt, CHANGE);	//interrupt 0 is on digital pin 2

	steering.attach(10);
	steering.writeMicroseconds(STEER_ADJUST);
	esc.attach(11);
	esc.writeMicroseconds(S1);

	Serial.println();
	Serial.println();

	//verify that car is in automatic mode
	get_mode();
	if(!automatic){
		Serial.println("1. SET CAR TO AUTOMATIC MODE! or press AUX to exit");
		Serial.println();
	}
	while(!automatic && !aux){
		get_mode();		//waits until the switch is flipped to start the race
		read_FIFO();
	}
	for(int i=0; i<100; i++){
		delay(1);
		read_FIFO();
	}

	//by turning off the radio, the automatic mode is locked in
	Serial.println("2. TURN OFF THE RADIO!");
	Serial.println();
	for(int i=0; i<500; i++){
		delay(1);
		read_FIFO();
	}

	Serial.println("***READY TO RUN***");
	Serial.println("3. FLIP THE SWITCH TO START THE RACE!");
	Serial.println();
	digitalWrite(12, HIGH);

	while(!digitalRead(TOGGLE) && (automatic)){	//waits for the switch to be flipped
		get_mode();		//waits until the switch is flipped to start the race
		read_FIFO();
	}
	for(int i=0; i<100; i++){	//waits 1 second before starting
		delay(1);
		read_FIFO();
	}

	wpr_count = 1;		//set waypoint read counter to first waypoint
	EEPROM_readAnything(wpr_count*WP_SIZE, waypoint);
	x_wp = waypoint.x;
	y_wp = waypoint.y;

	x=0;
	y=0;
	angle=0;
	clicks = 0;
	first = true;
	target_x = x_wp;
	target_y = y_wp;
}

void loop(){
	long temp;
	//watch_angle();
	read_FIFO();
	//watch_gyro();
	/* in the main loop here, we should wait for thing to happen, then act on them. Watch clicks and wait for it to reach CLICK_MAX, then calculate position and such.*/
	get_mode();

	if(clicks >= CLICK_MAX){
		clicks = 0;
		navigate();
	}

	if(automatic){	//this function get the car started moving and then clicks will take over
		if(!running){
			esc.write(S2);	//i don't understand this function...help...i changed this to S1 so the car is stationary?
			running = true;
		}
		if(first){
			angle = 0;
			first = false;
		}
	}
	
	if(manual){	//this function makes the car be stationary when in manual waypoint setting mode
		if(running){
			esc.write(S1);	//i changed this to S1 so the car is stationary?
			running = false;
		}
	}

	if(wpr_count >= WAYPOINT_COUNT){	//this locks the car into this loop and makes it stationary WHEN the course is completed
		esc.writeMicroseconds(S1);
		while(true);
	}
	
	if(aux){
		temp = millis();
		while(aux){
			get_mode();
			read_FIFO();
		}

		temp = millis() - temp;
		if(temp > 500) set_waypoint();
		//if(temp > 5000) read_waypoint();
	}
	
	if((millis()-time)>500){
		print_coordinates();
		time = millis();
	}
}

