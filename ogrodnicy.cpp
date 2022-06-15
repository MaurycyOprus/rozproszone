#include <math.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <pthread.h>
#include <list>
#include <vector>
#include <iostream>
#include <algorithm>
#include "colors.h"


// rank root
#define ROOT 0

// status.MPI_TAG - tagi wiadomosci
#define EMPTY_TAG 0
#define REQ_IN 1
#define ACK 2
#define REL_IN 3
#define REQ_T 4
#define REQ_P 5
#define REQ_W 6
#define REL_T 7
#define REL_P 8
#define REL_W 9

// parametry sekcji krytycznej
#define T_SIZE 4 // kosiarka
#define P_SIZE 2 // sekator
#define W_SIZE 2 // pryskacz

#define MAX_SIZE 16

int size;
int rank;
int local_queue[MAX_SIZE] = {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};
int T_queue[MAX_SIZE] = {-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2};
int P_queue[MAX_SIZE] = {-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2};
int W_queue[MAX_SIZE] = {-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2,-2};
int last_messages[MAX_SIZE] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
int total_tasks = 0;
int tasks_began = 0;
int timestamp = 0;
std::vector<int> task_eq;

// mutex
pthread_mutex_t mutex_init = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_task_queue = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_last_messages = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_second_cs = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_total_tasks = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_tasks_began = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_zegar = PTHREAD_MUTEX_INITIALIZER;

void instytut(int vNumOfGardeners){
    sleep(1);
    int dane [3];
    int task_id = 0;
    int time_sleep;
    int timestamp = 0;
    while (1)
    {
        time_sleep = (rand() % (10) + 8); // losowanie przerwy miedzy nowymi zleceniami
        printf(FBLU("(%d) INSTYTUT: informuje o nowym zleceniu (id zlecenia: %d)\n"), timestamp, task_id);
        dane[0] = task_id;
        dane[1] = rand()%3; // losowanie sprzetu potrzebnego do zlecenia 0 - kosiarka, 1 - sekator, 2 - preparat na szkodniki
        dane[2] = timestamp;
        for (int i = 1; i <= vNumOfGardeners; i++)
        {
            MPI_Send(&dane, sizeof(dane), MPI_INT, i, 0, MPI_COMM_WORLD); // informowanie ogrodnikow o zleceniach
        }
        timestamp += 1; //zwiekszenie wartosci zegara po wyslaniu

        printf(FBLU("(%d) INSTYTUT: wysylam nowe zlecenie za: %d sekund\n"), timestamp, time_sleep);
        sleep(time_sleep);

        task_id++; // kolejne zlecenie - zwiekszenie id
    }
}

void *listening(void *arguments){
    //////////////
    int number_of_gardeners = size - 1; 

    ///////////
    int task_data[3];
    int message_to_send[3];
    message_to_send[0] = 0;
    message_to_send[1] = 0;
    message_to_send[2] = 0;
    
    std::vector<int> tasks;

    int task_id, equipment_id;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Status status;
    while(1){
        MPI_Recv(&task_data, 12, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);    //  otrzymanie zlecenia
        sleep(1);
        if(task_data[2] > timestamp){   //  aktualizacja wartosci zegara
            timestamp = task_data[2] + 1;
        }else{
            timestamp += 1;
        }

        if (status.MPI_SOURCE == 0){    
            //  wiadomość od instytutu
            task_id = task_data[0];
            equipment_id = task_data[1];
            tasks.push_back(task_data[2]);         //   dodanie timestampu zlecenia do listy zleceń
            task_eq.push_back(equipment_id);
            printf(FGRN("(%d) OGRODNIK NR: %d otrzymal info o zleceniu nr: %d (Potrzebny sprzęt: %d)\n"), timestamp, rank, task_id, equipment_id);
            pthread_mutex_lock(&mutex_total_tasks);
            total_tasks++;
            pthread_mutex_unlock(&mutex_total_tasks);
        }else{                          
            //  wiadomość od ogrodnika
            pthread_mutex_lock(&mutex_last_messages); 
            last_messages[status.MPI_SOURCE-1] =  timestamp;
            pthread_mutex_unlock(&mutex_last_messages); 
            switch(status.MPI_TAG){
                case REQ_IN:
                    pthread_mutex_lock(&mutex_task_queue); 
                    local_queue[status.MPI_SOURCE-1] = task_data[2];    //  wstawienie do lokalnej kolejki czyjegość ządania - 2d
                    pthread_mutex_unlock(&mutex_task_queue); 
                    message_to_send[2] = timestamp;
                    MPI_Send(&message_to_send, 1, MPI_INT, status.MPI_SOURCE, ACK, MPI_COMM_WORLD); //    odeslanie ack
                break;
                case ACK:
                    // printf(FWHT("(%d) OGRODNIK NR: %d otrzymal ACK od: %d \n"), timestamp, rank, status.MPI_SOURCE);
                break;
                case REL_IN:
                    pthread_mutex_lock(&mutex_tasks_began);
                    tasks_began++;
                    pthread_mutex_unlock(&mutex_tasks_began);
                    local_queue[status.MPI_SOURCE-1] = -2;
                    MPI_Send(&message_to_send, 1, MPI_INT, status.MPI_SOURCE, ACK, MPI_COMM_WORLD);
                    // printf(FWHT("(%d) OGRODNIK NR: %d otrzymal RELEASE od: %d \n"), timestamp, rank, status.MPI_SOURCE);
                break;
                case REQ_T: // kosiarka
                    T_queue[status.MPI_SOURCE-1] = task_data[2];
                    // printf(FWHT("(%d) OGRODNIK NR: %d otrzymal REQUEST_T od: %d \n"), timestamp, rank, status.MPI_SOURCE);
                    MPI_Send(&message_to_send, 1, MPI_INT, status.MPI_SOURCE, ACK, MPI_COMM_WORLD); //    odeslanie ack
                break;
                case REQ_P: // sekator
                    P_queue[status.MPI_SOURCE-1] = task_data[2];
                    // printf(FWHT("(%d) OGRODNIK NR: %d otrzymal REQUEST_P od: %d \n"), timestamp, rank, status.MPI_SOURCE);
                    MPI_Send(&message_to_send, 1, MPI_INT, status.MPI_SOURCE, ACK, MPI_COMM_WORLD); //    odeslanie ack
                break;
                case REQ_W: // pryskacz
                    W_queue[status.MPI_SOURCE-1] = task_data[2];
                    // printf(FWHT("(%d) OGRODNIK NR: %d otrzymal REQUEST_W od: %d \n"), timestamp, rank, status.MPI_SOURCE);
                    MPI_Send(&message_to_send, 1, MPI_INT, status.MPI_SOURCE, ACK, MPI_COMM_WORLD); //    odeslanie ack
                break;
                case REL_T: // kosiarka
                    T_queue[status.MPI_SOURCE-1] = -2;
                    local_queue[status.MPI_SOURCE-1] = -1;
                    MPI_Send(&message_to_send, 1, MPI_INT, status.MPI_SOURCE, ACK, MPI_COMM_WORLD);
                    // printf(FWHT("(%d) OGRODNIK NR: %d otrzymal RELEASE_T od: %d \n"), timestamp, rank, status.MPI_SOURCE);
                break;
                case REL_P: // sekator
                    P_queue[status.MPI_SOURCE-1] = -2;
                    local_queue[status.MPI_SOURCE-1] = -1;
                    MPI_Send(&message_to_send, 1, MPI_INT, status.MPI_SOURCE, ACK, MPI_COMM_WORLD);
                    // printf(FWHT("(%d) OGRODNIK NR: %d otrzymal RELEASE_P od: %d \n"), timestamp, rank, status.MPI_SOURCE);
                break;
                case REL_W: // pryskacz
                    W_queue[status.MPI_SOURCE-1] = -2;
                    local_queue[status.MPI_SOURCE-1] = -1;
                    MPI_Send(&message_to_send, 1, MPI_INT, status.MPI_SOURCE, ACK, MPI_COMM_WORLD);
                    // printf(FWHT("(%d) OGRODNIK NR: %d otrzymal RELEASE_W od: %d \n"), timestamp, rank, status.MPI_SOURCE);
                break;
                default:
                break;
            }
        }
    }
}


int main(int argc, char **argv)
{
    srand(time(NULL));
    int provided;
 
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Status status;


    //model wiadomości do innych ogrodników
    int message_for_gardeners[3];
    message_for_gardeners[0] = 0;
    message_for_gardeners[1] = 0;
    message_for_gardeners[2] = 0;
    
    //model wiadomości o ukończeniu zlecenia
    int message_task_complete[2];
    
    //liczba urządzeń
    int T_size = T_SIZE;
    int P_size = P_SIZE;
    int W_size = W_SIZE;


    // index 0: liczba kosiarek, index 1: liczba sekatorow, index 2: liczba preparatow
    int number_of_gardeners = size - 1; // liczba ogrodnikow
    if(rank == ROOT){       
        //Instytut, rozsylanie zlecen
        instytut(number_of_gardeners);
    }else{
        //Ogrodnicy
 
        //Stworzenie watku do nasluchiwania
        local_queue[rank - 1] = -1;
        pthread_t thread_id;
        pthread_create(&thread_id, NULL, listening, NULL);

        // zainicjalizowani ogrodnicy
        int len;
        char processor[100];
        MPI_Get_processor_name(processor, &len);
        printf("Ogrodnik: %d of %d na (%s)\n", rank, size - 1, processor);
        
        //lokalna kolejka do zlecen od instytutu

        while(1){

            //jeśli jestem poza kolejką
            if(local_queue[rank-1] == -1){
                message_for_gardeners[2] = timestamp;
                for (int i = 1; i <= number_of_gardeners; i++){
                    if(i != rank)
                        MPI_Send(&message_for_gardeners, 12, MPI_INT, i, REQ_IN, MPI_COMM_WORLD); // wysłanie req do wszystkich - punkt 2c
                    sleep(1);
                }
                MPI_Send(&message_for_gardeners, 12, MPI_INT, rank, REQ_IN, MPI_COMM_WORLD); // wysłanie req do wszystkich - punkt 2c
                timestamp++;
            }
            sleep(1);
            //sprawdzenie czy otrzymałem nowszą wiadomość od wszystkich - 2e
            pthread_mutex_lock(&mutex_last_messages); 
            int v_true = 1;
            for (int i = 1; i <= number_of_gardeners; i++){
                if(i != rank && last_messages[i-1] <= last_messages[rank-1]){
                    // printf(FWHT("(%d) OGRODNIK NR: %d ma starsza wiadomosc niz ja (NR OGR.: %d) \n"), timestamp, i, rank);
                    v_true = 0;
                    // printf(FWHT("(%d) Lokalna kolejna OGRODNIKA NR: %d [%d, %d, %d, %d]\n"), timestamp, rank, local_queue[0], local_queue[1], local_queue[2], local_queue[3]);
                    // printf(FBLU("(%d) Timestampy: [%d, %d, %d, %d]\n"), timestamp,  last_messages[0], last_messages[1], last_messages[2], last_messages[3]);
                }
            }
            pthread_mutex_unlock(&mutex_last_messages); 

            //jeśli mam nowszą wiadomość od wszystkich, to sprawdzaj swoją pozycję w kolejce - 2e
            if (v_true == 1 && local_queue[rank-1] >= 0){
                // printf(FWHT("(%d) OGRODNIK NR: %d odebral nowsze wiadomosci od każdego procesu\n"), timestamp, rank);
                pthread_mutex_lock(&mutex_task_queue); 
                int my_position = 1;
                for (int i = 1; i <= number_of_gardeners; i++){
                    if(i != rank && local_queue[i-1] >= 0 && (local_queue[i-1] < local_queue[rank-1] || (local_queue[i-1] == local_queue[rank-1] &&  i < rank))){
                        my_position++;
                    }
                }
                pthread_mutex_unlock(&mutex_task_queue); 

                printf(FWHT("(%d) OGRODNIK NR: %d ma %d pozycje w lokalnej kolejce\n"), timestamp, rank, my_position);
                printf(FWHT("(%d) Lokalna kolejna OGRODNIKA NR: %d [ "), timestamp, rank);
                for(int i=0; i < size - 1; i++){
                    printf(FWHT(" %d "), local_queue[i]);
                }
                printf(" ]\n");
                printf(FWHT("(%d) Timestampy kontrolne: [ "), timestamp);
                for(int i=0; i < size - 1; i++){
                    printf(FWHT(" %d "), last_messages[i]);
                }
                printf(" ]\n");
                printf(FBLK("Total tasks: %d, tasks began: %d\n"), total_tasks, tasks_began);

                //biore zadanie odpowiadajace mojej pozycji, jezeli takie jest - 2e
                if(total_tasks - tasks_began > 0 && my_position < total_tasks - tasks_began){
                    pthread_mutex_lock(&mutex_init);
                    pthread_mutex_lock(&mutex_tasks_began);
                    printf(FMAG("(%d) OGRODNIK NR: %d bierze zlecenie nr %d\n"), timestamp, rank, tasks_began + my_position - 1);
                    int my_task = tasks_began + my_position - 1;
                    pthread_mutex_unlock(&mutex_tasks_began);
                    message_for_gardeners[2] = timestamp;
                    for (int i = 1; i <= number_of_gardeners; i++){
                        MPI_Send(&message_for_gardeners, 1, MPI_INT, i, REL_IN, MPI_COMM_WORLD); // wyslanie release po otrzymaniu zadania - 2e
                    }
                    timestamp++;
                    pthread_mutex_unlock(&mutex_init);  
                    int sleep_time = (rand() % (10) + 5);
                    printf(FMAG("(%d) OGRODNIK NR: %d zapoznaje się z literaturą przez kolejne %d sekund\n"), timestamp, rank, sleep_time);
                    sleep(sleep_time);
                    int tag = 0;

                    //wyslanie odpowiedniego rodzaju req do wszystkich - 2f
                    if(task_eq[my_task] == 0){
                        tag = REQ_T;
                        int my_eq_size = T_SIZE;
                    }else if (task_eq[my_task] == 1){
                        tag = REQ_P;
                        int my_eq_size = P_SIZE;
                    }else if (task_eq[my_task] == 2){
                        tag = REQ_W;
                        int my_eq_size = W_SIZE;
                    }
                    message_for_gardeners[2] = timestamp;
                    for (int i = 1; i <= number_of_gardeners; i++){
                        MPI_Send(&message_for_gardeners, 12, MPI_INT, i, tag, MPI_COMM_WORLD); // wyslanie release po otrzymaniu zadania - 2e
                    }
                    // printf(FREV("Timestamp do REQ(T/P/W): %d\n"), timestamp);
                    timestamp++;

                    //----------------------------------------------------------------------------------------------------------------------------------------------------
                    int task_finished = 0;
                    while(task_finished == 0){
                        pthread_mutex_lock(&mutex_last_messages); 
                    //sprawdzenie czy mam nowszą wiadomość od wszystkich - 2f
                        int v_true = 1;
                        for (int i = 1; i <= number_of_gardeners; i++){
                            if(i != rank && last_messages[i-1] <= last_messages[rank-1]){
                                v_true = 0;
                            }
                        }
                        pthread_mutex_unlock(&mutex_last_messages); 

                        //sprawdzenie pozycji w kolejce do odpowiedniego sprzętu - 2f
                        if (v_true == 1){
                            pthread_mutex_lock(&mutex_second_cs); 
                            int my_position = 1;
                            if(tag == REQ_T){
                                for (int i = 1; i <= number_of_gardeners; i++){
                                    if(i != rank && T_queue[i-1] >= 0 && (T_queue[i-1] < T_queue[rank-1] || (T_queue[i-1] == T_queue[rank-1] &&  i < rank))){
                                        my_position++;
                                    }
                                }
                                printf(FCYN("(%d) OGRODNIK NR: %d jest na pozycji %d w kolejce po kosiarke. Kolejka: [%d, %d, %d, %d, %d, %d, %d, %d]\n"), timestamp, rank, my_position, T_queue[0], T_queue[1], T_queue[2], T_queue[3], T_queue[4], T_queue[5], T_queue[6], T_queue[7]);
                            }else if(tag == REQ_P){
                                for (int i = 1; i <= number_of_gardeners; i++){
                                    if(i != rank && P_queue[i-1] >= 0 && (P_queue[i-1] < P_queue[rank-1] || (P_queue[i-1] == P_queue[rank-1] &&  i < rank))){
                                        my_position++;
                                    }
                                }
                                printf(FRED("(%d) OGRODNIK NR: %d jest na pozycji %d w kolejce po sekator. Kolejka: [%d, %d, %d, %d, %d, %d, %d, %d]\n"), timestamp, rank, my_position, P_queue[0], P_queue[1], P_queue[2], P_queue[3], P_queue[4], P_queue[5], P_queue[6], P_queue[7]);
                            }else{
                                for (int i = 1; i <= number_of_gardeners; i++){
                                    if(i != rank && W_queue[i-1] >= 0 && (W_queue[i-1] < W_queue[rank-1] || (W_queue[i-1] == W_queue[rank-1] &&  i < rank))){
                                        my_position++;
                                    }
                                }
                                printf(FYEL("(%d) OGRODNIK NR: %d jest na pozycji %d w kolejce po pryskacz. Kolejka: [%d, %d, %d, %d, %d, %d, %d, %d]\n"), timestamp, rank, my_position, W_queue[0], W_queue[1], W_queue[2], W_queue[3],  W_queue[4], W_queue[5], W_queue[6], W_queue[7]);
                            }
                            pthread_mutex_unlock(&mutex_second_cs); 

                            //sprawdzenie czy moja pozycja upoważnia mnie do wzięcia sprzętu - 2f
                            if(tag == REQ_T && my_position <= T_SIZE){
                                int sleep_time = (rand() % (10) + 5);
                                printf(FCYN("(%d) OGRODNIK NR: %d bierze wchodzi do sekcji krytycznej i zaczyna realizować zlecenie %d (przez %d sekund)\n"), timestamp, rank, my_task, sleep_time);
                                sleep(sleep_time);      //wykonywanie zadania
                                task_finished = 1;
                                local_queue[rank-1] = -1;
                                printf(FREV("(%d) OGRODNIK NR: %d zakończył wykonywanie zadania %d\n"), timestamp, rank, my_task);
                                message_for_gardeners[2] = timestamp;
                                for (int i = 1; i <= number_of_gardeners; i++){
                                    MPI_Send(&message_for_gardeners, 12, MPI_INT, i, REL_T, MPI_COMM_WORLD);
                                }
                                timestamp++;
                            }else if(tag == REQ_P && my_position <= P_SIZE){
                                int sleep_time = (rand() % (10) + 5);
                                printf(FRED("(%d) OGRODNIK NR: %d bierze wchodzi do sekcji krytycznej i zaczyna realizować zlecenie %d (przez %d sekund)\n"), timestamp, rank, my_task, sleep_time);
                                sleep(sleep_time);      //wykonywanie zadania
                                task_finished = 1;
                                local_queue[rank-1] = -1;
                                printf(FREV("(%d) OGRODNIK NR: %d zakończył wykonywanie zadania %d\n"), timestamp, rank, my_task);
                                message_for_gardeners[2] = timestamp;
                                for (int i = 1; i <= number_of_gardeners; i++){
                                    MPI_Send(&message_for_gardeners, 12, MPI_INT, i, REL_P, MPI_COMM_WORLD);
                                }
                                timestamp++;
                            }else if(tag == REQ_W && my_position <= W_SIZE){
                                int sleep_time = (rand() % (10) + 5);
                                printf(FYEL("(%d) OGRODNIK NR: %d bierze wchodzi do sekcji krytycznej i zaczyna realizować zlecenie %d (przez %d sekund) \n"), timestamp, rank, my_task, sleep_time);
                                sleep(sleep_time);      //wykonywanie zadania
                                task_finished = 1;
                                local_queue[rank-1] = -1;
                                printf(FREV("(%d) OGRODNIK NR: %d zakończył wykonywanie zadania %d\n"), timestamp, rank, my_task);
                                message_for_gardeners[2] = timestamp;
                                for (int i = 1; i <= number_of_gardeners; i++){
                                    MPI_Send(&message_for_gardeners, 12, MPI_INT, i, REL_W, MPI_COMM_WORLD);
                                }
                                timestamp++;
                            }
                        }else{
                            // printf(FBLU("(%d) OGRODNIK NR: %d nie otrzymal wiadomości o wyzszym timestampie od kazdego ogrodnika, timestampy: [%d, %d, %d, %d]\n"), timestamp, rank, last_messages[0], last_messages[1], last_messages[2], last_messages[3]);
                            sleep(1);
                        }
                    }
                    
                }
            }else{
                // printf(FWHT("(%d) OGRODNIK NR: %d nie otrzymal wiadomości o wyzszym timestampie od kazdego ogrodnika, timestampy: [%d, %d, %d, %d]\n"), timestamp, rank, last_messages[0], last_messages[1], last_messages[2], last_messages[3]);
                sleep(1);
            }
        }
    }

    MPI_Finalize();
    return 0;
}
