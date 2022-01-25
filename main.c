/*
 * File:   main.c
 * Author: johan
 *
 * Created on 30 de Maio de 2021, 16:52
 */


#define F_CPU 16000000      //define a frequencia do uC para 16MHz
#include <avr/io.h>         //Biblioteca geral dos AVR
#include <avr/interrupt.h>  //Biblioteca de interrup��o
#include <stdio.h>          //Bilioteca do C
#include <util/delay.h>     //Biblioteca geradora de atraso
#include "UART.h"           //Biblioteca da comunica��o UART
#include "ADC.h"            //Biblioteca do conversor AD
#include "configbits.txt"   //configura os fus�veis

//vari�veis de comando para os registradores
#define set_bit(y,bit) (y|=(1<<bit)) //coloca em 1 o bit x da vari�vel Y
#define clr_bit(y,bit) (y&=~(1<<bit)) //coloca em 0 o bit x da vari�vel Y
#define cpl_bit(y,bit) (y^=(1<<bit)) //troca o estado l�gico do bit x da vari�vel Y
#define tst_bit(y,bit) (y&(1<<bit)) //retorna 0 ou 1 conforme leitura do bit


//Lado direito
#define AIN2 PD6// Quando em HIGH, roda direita anda para frente
#define AIN1 PD5 

//Lado Esquerdo
#define BIN1 PD4 // Quando em HIGH, roda esquerda anda para frente
#define BIN2 PD3

int Kp = 38; //prescale de 100 - prescale int
int Kd = 82; //prescale de 100 - prescale int
int Ki = 0; // Vari�veis que s�o modificadas no PID - prescale de 100 ou mais
int PWMR = 200; // valor da for�a do motor em linha reta
int PWM_Curva = 150; //PWM ao entrar na curva
int erro, p, d, erroAnterior = 0, i, integral = 0, Turn = 0; //�rea PID -erro tamb�m possui prescale = 100
int u = 0; //valor de retorno do PID
int u_curva = 0;
int prescale = 2000; //prescale das constantes * prescale do erro
int PWMA, PWMB; // Modula��o de largura de pulso enviada pelo PID
int PWMA_C, PWMB_C; //PWM de curva com ajuste do PID
int entrou;
int contador = 0, acionador = 0; // Borda
int marcadores = 6;
int erroAnterior_curva = 0;
int Turn_curva, p_curva, d_curva, i_curva, integral_curva = 0;


char s [] = "In�cio da leitura";
char buffer[5]; //String que armazena valores de entrada para serem printadas
volatile char ch; //armazena o caractere lido
volatile char flag_com = 0; //flag que indica se houve recep��o de dado
// Interrup��o da UART
//======================================================


/*tempo =65536 ? Prescaler/Fosc = 65536 ? 1024/16000000 = 4, 19s
 tempo = X_bit_timer * Prescaler/Fosc
 Valor inicial de contagem = 256 ? tempo_desejado?Fosc/Prescaler = 256 ? 0,01?16000000/1024 = 98,75 ? 99
 Valor inicial de contagem = X_bit_timer - tempo_desejado*Fosc/Prescaler*/


unsigned int millis = 0;

ISR(TIMER0_OVF_vect) {
    TCNT0 = 255; //Recarrega o Timer 0 para que a contagem seja 1ms novamente
    millis++; //Incrementa a vari�vel millis a cada 10ms
}

ISR(USART_RX_vect) {
    ch = UDR0; //Faz a leitura do buffer da serial

    UART_enviaCaractere(ch); //Envia o caractere lido para o computador
    flag_com = 1; //Aciona o flag de comunica��o
}
//------------------------------------------------------

void setDuty_1(int duty); //Seleciona o duty cycle na sa�da digital  3
void setFreq(char option); //Seleciona a frequ�ncia de opera��o do PWM
void setDuty_2(int duty);
int PID(int error, int tempo);
void frente();
void tras();
void esquerda();
void direita();
void motor_off();
void freio();
int entrouCurva(int sensor, int frontal, int valor_erro, int tempo_passado);
int PID_Curva(int error_curva, int tempo_curva);

int main(void) {
    unsigned int delta_T = 0;
    int peso [] = {-3, -2, -1, 1, 2, 3};
    int soma_direito, soma_esquerdo;
    int denominador_direito = 6;
    int denominador_esquerdo = 6;
    int soma_total = 0;

    DDRD = 0b01111000; //PD6 - PD3 definidos como sa�da
    PORTD = 0b00000000; //inicializados em n�vel baixo
    DDRB = 0b00100110; //Habilita PB1 e PB2 e PB5 como sa�da   PB1 e PB2 s�o portas PWM
    PORTB = 0b00000000; //PORTB inicializa desligado e sa�das sem pull up

    UART_config(); //Inicializa a comunica��o UART
    inicializa_ADC(); //Configura o ADC
    UART_enviaString(s); //Envia um texto para o computador

    TCCR0B = 0b00000101; //TC0 com prescaler de 1024
    TCNT0 = 255; //Inicia a contagem em 100 para, no final, gerar 1ms
    TIMSK0 = 0b00000001; //habilita a interrup��o do TC0

    TCCR1A = 0xA2; //Configura opera��o em fast PWM, utilizando registradores OCR1x para compara��o

    setFreq(3); //Seleciona op��o para frequ�ncia


    sei(); //Habilita as interrup��es


    //----> Calibra��o dos Sensores frontais <----\\

    for (int i = 0; i < 120; i++) {
        int sensores_frontais[] = {le_ADC(0), le_ADC(1), le_ADC(2), le_ADC(3), le_ADC(4), le_ADC(6)};
        //qtra.calibrate();
        /*
        Printar no serial (em um outro c�digo) os valores pra ver o m�ximo e m�nimo de cada sensor,
        porque n�o necessariamente eles chegam em 0 e 1023.
        (Alterar o limite do conversor AD atrav�s de ifs no main)
        Ap�s isso determinar o limiar de todos os sensores para que eles tenham os mesmos valores do AD. 
        Para que todos tenham um limite inferior e superior igual.
         */
        _delay_ms(5);
    }

    set_bit(PORTB, PB5); //subrotina de acender e apagar o LED 13
    _delay_ms(1000);
    clr_bit(PORTB, PB5);
    _delay_ms(500);
    set_bit(PORTB, PB5);
    _delay_ms(500);
    clr_bit(PORTB, PB5);

    while (1) {
        delta_T = millis;
        int sensores_frontais[] = {le_ADC(0), le_ADC(1), le_ADC(2), le_ADC(3), le_ADC(4), le_ADC(6)};
        int sensor_borda = le_ADC(5);


        for (int i = 0; i < 7; i++) {
            sprintf(buffer, "%4d", sensores_frontais[i]); //Converte para string
            UART_enviaString(buffer); //Envia para o computador
            UART_enviaCaractere(0x20); //espa�o
        }
        UART_enviaCaractere(0x0D); //pula linha

        //regi�o que seta os valores nos sensores frontais ap�s a calibra��o

        //----------------------------------------------------------------//

        for (int j = 0; j < 3; j++) {
            soma_esquerdo += (sensores_frontais[j] * peso[j]);
            soma_direito += (sensores_frontais[5 - j] * peso[5 - j]);
        }

        soma_total = (soma_esquerdo + soma_direito) / (denominador_esquerdo + denominador_direito);

        erro = 0 - soma_total; //valor esperado(estar sempre em cima da linha) - valor medido

        sprintf(buffer, "%4d\n", erro); //Converte para string
        UART_enviaString(buffer); //Envia para o computador
        UART_enviaCaractere(0x0D); //pula linha

        //--------------->AREA DO PID<---------------

        u = PID(erro, delta_T);

        PWMA = PWMR - u;
        PWMB = PWMR + u;

        //--------------->AREA DO SENSOR DE PARADA<---------------

        if ((sensor_borda < 300) && (acionador == 0) && soma_total > 100) {
            contador++;
            acionador = 1;
        } else if ((sensor_borda > 500) && (acionador == 1)) {
            acionador = 0;
        } else if ((sensor_borda > 500) && soma_total < 100) //situa��o de cruzamento
        {
            acionador = 0;
        }

        //fun��o freio
        while (contador >= marcadores) {
            freio();
        }

        //-----> �rea do senstido de giro

        if (erro < 0) //virar para a esquerda
        {
            entrouCurva(sensor_borda, soma_total, erro, delta_T);
            set_bit(PORTB, PB5); //liga o LED
            while (erro < 0) {
                esquerda();
            }
            clr_bit(PORTB, PB5);

        } else if (erro > 0) {
            entrouCurva(sensor_borda, soma_total, erro, delta_T);
            set_bit(PORTB, PB5); //liga o LED
            while (erro > 0) {
                direita();
            }
            clr_bit(PORTB, PB5);
        }

        //A fun��o que fazia o rob� rodar em seu pr�prio eixo foi removida


        //------> Limitando PWM

        if (PWMA > 255) {
            PWMA = 250;
        } else if (PWMB > 255) {
            PWMB = 250;
        }


    }
}

void setDuty_1(int duty) //MotorA
{

    OCR1B = duty;

} //end setDuty_pin3

void setDuty_2(int duty) //MotorB
{

    OCR1A = duty; //valores de 0 - 1023

} //end setDuty_pin3

void setFreq(char option) {
    /*
    TABLE:
  
        option  frequency (as frequ�ncias no timer 1 s�o menores do que as frequ�ncias nos timers 0 e 2)
        
          1      16    kHz
          2       2    kHz
          3     250     Hz
          4     62,5    Hz
          5     15,6    Hz
     */
    TCCR1B = option;


} //end setFrequency

int PID(int error, int tempo) {
    p = (error * Kp) / prescale; // Proporcao

    integral += error; // Integral
    i = ((Ki * integral) / prescale) * tempo;

    d = ((Kd * (error - erroAnterior)) / prescale) / tempo; // Derivada
    erroAnterior = error;

    Turn = p + i + d;
    return Turn; //retorna os valores ap�s o PID
}

void frente() {

    clr_bit(PORTD, AIN1);
    set_bit(PORTD, AIN2); //frente direita
    clr_bit(PORTD, BIN2);
    set_bit(PORTD, BIN1); //frente esquerda
}

void tras() {
    set_bit(PORTD, AIN1);
    clr_bit(PORTD, AIN2); //frente direita
    clr_bit(PORTD, BIN2);
    set_bit(PORTD, BIN1); //frente esquerda

}

void motor_off() {
    clr_bit(PORTD, AIN1);
    clr_bit(PORTD, AIN2);
    clr_bit(PORTD, BIN2);
    clr_bit(PORTD, BIN1);
}

void freio() {
    frente();

    setDuty_1(50);
    setDuty_2(50);

    _delay_ms(500);

    tras();

    setDuty_1(10);
    setDuty_2(10);

    _delay_ms(5);

    frente();

    setDuty_1(0);
    setDuty_2(0);

    _delay_ms(2000);

    motor_off();

    _delay_ms(60000);
}

void direita() {
    set_bit(PORTD, AIN1); //tras direita
    clr_bit(PORTD, AIN2);
    clr_bit(PORTD, BIN2);
    set_bit(PORTD, BIN1); //frente esquerda

    setDuty_1(PWMA_C);
    setDuty_2(PWMB_C);

    //calibra��o dos sensores frontais - seta o valor m�dio
}

void esquerda() {
    clr_bit(PORTD, AIN1);
    set_bit(PORTD, AIN2); //direita frente
    clr_bit(PORTD, BIN2);
    set_bit(PORTD, BIN1); //esquerda tr�s

    setDuty_1(PWMA_C);
    setDuty_2(PWMB_C);


    //calibra��o dos sensores frontais - seta o valor m�dio
}

int entrouCurva(int sensor, int frontal, int valor_erro, int tempo_passado) {
    if (sensor < 550 && frontal > 100) {
        switch (entrou) {
            case 0: //entrou na curva
                u_curva = PID_Curva(valor_erro, tempo_passado);
                PWMA_C = PWM_Curva - u_curva;
                PWMB_C = PWM_Curva + u_curva;
                setDuty_1(PWMA_C);
                setDuty_2(PWMB_C);
                entrou = 1;
                break;

            case 1:
                entrou = 0;
                setDuty_1(PWMA);
                setDuty_2(PWMB);
                break;
        }
    } else if (sensor > 550 && frontal < 100) { //regi�o de cruzamento
        setDuty_1(PWMA);
        setDuty_2(PWMB);
    }
}

int PID_Curva(int error_curva, int tempo_curva) {
    p_curva = (error_curva * Kp) / prescale; // Proporcao

    integral_curva += error_curva; // Integral
    i_curva = ((Ki * integral_curva) / prescale) * tempo_curva;

    d_curva = ((Kd * (error_curva - erroAnterior_curva)) / prescale) / tempo_curva; // Derivada
    erroAnterior_curva = error_curva;

    Turn_curva = p_curva + i_curva + d_curva;
    return Turn_curva; //retorna os valores ap�s o PID
}