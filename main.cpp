#include "mbed.h"
#include <string>

using namespace std;

DigitalOut trigger(D7);
InterruptIn echo(D6);
DigitalIn ir_sensor(A0); // Sensore IR collegato a un pin analogico
BufferedSerial pc(USBTX, USBRX, 9600);

Timer timer;
volatile float measured_distance = 0.0;
volatile bool measurement_ready = false;

int score = 0;
int lives = 3;

int threshold_distance = 0;
bool game_over = false;
bool measuring = false;

bool running = false;
bool red_light_active = false;

enum GameMode { NO_GAME, DISTANCE_MATCH, DODGE_OBSTACLE };

GameMode current_game_mode = NO_GAME;

void echo_rise() {
    timer.start();
}

void echo_fall() {
    timer.stop();
    float time = timer.read_us();
    measured_distance = (time * 0.0343) / 2.0;
    timer.reset();
    measurement_ready = true;
}

void measure_distance() {
    measurement_ready = false;
    trigger = 1;
    wait_us(10);
    trigger = 0;
}

const char* get_game_mode_str(GameMode mode) {
    switch (mode) {
        case NO_GAME: return "NO_GAME";
        case DISTANCE_MATCH: return "DISTANCE_MATCH";
        case DODGE_OBSTACLE: return "DODGE_OBSTACLE";
        default: return "UNKNOWN";
    }
}

void start_new_round() {
    switch (current_game_mode){
        case DISTANCE_MATCH: {
            threshold_distance = rand() % 41 + 10;  // Distanza casuale tra 10 e 50 cm
            char buffer[100];
            int len = sprintf(buffer, "{\"status\": \"NEW_ROUND\", \"game_mode\": \"%s\", \"score\": %d, \"lives\": %d, \"threshold_distance\": %d}\n", get_game_mode_str(current_game_mode), score, lives, threshold_distance);
            pc.write(buffer, len);
            ThisThread::sleep_for(2s);
            measuring = false;
            break;
        }
        case DODGE_OBSTACLE: {
                red_light_active = false;
                running = ir_sensor.read() == 1;
                char buffer[100];
                int len = sprintf(buffer, "{\"status\": \"NEW_ROUND\", \"score\": %d, \"lives\": %d}\n", score, lives);
                pc.write(buffer, len);
            break;
        }
        default: {
            break;
        }
    }
}

void check_command() {
    char buffer[20];
    if (pc.readable()) {
        int len = pc.read(buffer, sizeof(buffer));
        buffer[len] = '\0';

        if (strcmp(buffer, "DM") == 0) {
            current_game_mode = DISTANCE_MATCH;   
        }else if (strcmp(buffer, "DO") == 0) {
            current_game_mode = DODGE_OBSTACLE;
        } 

        if (strcmp(buffer, "START") == 0) {
            measuring = true;
        } else if (strcmp(buffer, "STOP") == 0) {
            measuring = false;
        } else if(strcmp(buffer, "RESTART") == 0) {
            score = 0;
            lives = 3;
            game_over = false;
            start_new_round();
        }
    }
}

void trigger_red_light() {
    red_light_active = true;
    char buffer[100];
    int len = sprintf(buffer, "{\"status\": \"RED_LIGHT\",\"score\": %d, \"lives\": %d}\n",score,lives);
    pc.write(buffer, len);
    ThisThread::sleep_for(2s); // Tempo durante il quale la "Luce Rossa" è attiva
    red_light_active = false;
}


void process_game() {
    char buffer[150];

    switch(current_game_mode) {
        case DISTANCE_MATCH: {
            if (measuring) {
                measure_distance();
                ThisThread::sleep_for(100ms);
                if (measurement_ready) {
                    char buffer[100];
                    int len = sprintf(buffer, "{\"status\": \"MEASURING\", \"distance\": %d,\"score\": %d, \"lives\": %d}\n", (int)measured_distance, score, lives);
                    pc.write(buffer, len);
                }
            } else if (measurement_ready) {
                char buffer[150];

                if (measured_distance < threshold_distance) {
                    lives--;
                    ThisThread::sleep_for(500ms);
                    int len = sprintf(buffer, "{\"status\": \"ERROR\", \"lives\": %d, \"threshold_distance\": %d, \"distance\": %d, \"score\": %d}\n", lives, threshold_distance, (int)measured_distance, score);
                    pc.write(buffer, len);
                } else {
                    int points = 0;
                    if (measured_distance <= 50) {
                        points = 100 - abs(threshold_distance - (int)measured_distance) * 2;
                        points = (points < 0) ? 0 : points;
                    }
                    score += points;
                    int len = sprintf(buffer, "{\"status\": \"SUCCESS\", \"round_points\": %d, \"threshold_distance\": %d, \"distance\": %d, \"score\": %d}\n", points, threshold_distance, (int)measured_distance, score);
                    pc.write(buffer, len);
                } 
                ThisThread::sleep_for(3s);
                measurement_ready = false;

            if (lives <= 0) {
                game_over = true;
                int len = sprintf(buffer, "{\"status\": \"GAME_OVER\", \"score\": %d,\"lives\": %d}\n", score, lives);
                pc.write(buffer, len);
            } else {
                start_new_round();
            }
        }
            break;
        }
        case DODGE_OBSTACLE: {
                if ((rand() % 10) < 1) { // 10% di probabilità ogni ciclo
                trigger_red_light();
            }
                if (red_light_active) {
                    ThisThread::sleep_for(1000ms);
                    if (ir_sensor.read() == 1) { // Mano vicino al sensore
                        lives--;
                        char buffer[100];
                        int len = sprintf(buffer, "{\"status\": \"HIT\", \"score\": %d, \"lives\": %d}\n",score, lives);
                        pc.write(buffer, len);
                        if (lives <= 0) {
                            game_over = true;
                            len = sprintf(buffer, "{\"status\": \"GAME_OVER\", \"score\": %d,\"lives\": %d}\n", score, lives);
                            pc.write(buffer, len);
                        }
                    }
                } else {
                    if (ir_sensor.read() == 1) { // Mano vicino al sensore
                        score++;
                    }
            }
                if (lives <= 0) {
                game_over = true;
                int len = sprintf(buffer, "{\"status\": \"GAME_OVER\", \"score\": %d,\"lives\": %d}\n", score, lives);
                pc.write(buffer, len);
            } else {
                start_new_round();
            }
            break;
        }
        default: {
            break;
        }
    }
}

int main() {
    Timer t;
    t.start();
    srand(t.read_us());

    echo.rise(&echo_rise);
    echo.fall(&echo_fall);

    start_new_round();

    while (1) {
        check_command();

        if (game_over) {
            ThisThread::sleep_for(1s);
            continue;
        }

        process_game();

        ThisThread::sleep_for(10ms);
    }
}
