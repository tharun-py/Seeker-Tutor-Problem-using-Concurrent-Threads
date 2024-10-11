#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <limits.h>
#include <time.h>

typedef struct Learner
{
    int learner_id;
    int sessions_received;
    int state;
    int time_of_arrival;
} Learner;

/*
State values indicate the following
-1 : Not in Waiting Room
-2 : Inside Waiting Room, but waiting to be processed by Coordinator
-3 : Inside Waiting Room, processed by Coordinator, waiting to be picked by an Instructor
 0 : Indicates that this particular learner is currently in a session
*/

int total_learners;    // Total number of Learners
int total_instructors; // Total number of Instructors
int available_seats;   // Total number of available seats in the waiting room
int session_limit;     // Maximum number of sessions allowed per learner

Learner *learners;       // Array to store current attributes of each learner
int *instructor_ids;     // Array to store the IDs of each instructor
int *waiting_room_queue; // Queue of learners waiting in the room
int num_learners_in_queue;

int *learner_in_session_by; // Mapping learners to their respective instructors

int free_seats;
int total_requests_to_coordinator;
int current_sessions_in_progress;
int total_completed_sessions;
int total_learners_done;
int global_clock;

pthread_mutex_t resource_lock; // Mutex to lock shared resources

// Semaphores
sem_t *coord_sem; // Semaphore for learner-coordinator interaction
sem_t *instr_sem; // Semaphore for coordinator-instructor interaction

void *learner_thread(void *arg)
{
    int learner_id = *(int *)arg;

    while (1)
    {
        if (learners[learner_id].sessions_received >= session_limit)
        {
            pthread_mutex_lock(&resource_lock);
            total_learners_done++;

            if (total_learners_done == total_learners)
            {
                sem_post(coord_sem);
            }

            pthread_mutex_unlock(&resource_lock);
            pthread_exit(NULL);
        }

        pthread_mutex_lock(&resource_lock);

        if (free_seats > 0)
        {
            free_seats--; // Learner takes a seat in the waiting room
            num_learners_in_queue++;
            waiting_room_queue[num_learners_in_queue - 1] = learner_id;

            printf("Learner %d takes a seat. Remaining seats = %d.\n", learner_id, free_seats);

            learners[learner_id].state = -2;
            learners[learner_id].time_of_arrival = global_clock++;
            total_requests_to_coordinator++;

            sem_post(coord_sem);
            pthread_mutex_unlock(&resource_lock);

            while (learners[learner_id].state != -1)
            {
                usleep(200);
            }

            pthread_mutex_lock(&resource_lock);

            int instructor_id = learner_in_session_by[learner_id]; // Getting the instructor ID
            printf("Learner %d received instruction from Instructor %d.\n", learner_id, instructor_id);
            learner_in_session_by[learner_id] = -1; // Reset learner's tutor
            learners[learner_id].time_of_arrival = INT_MAX;

            pthread_mutex_unlock(&resource_lock);
        }
        else
        {
            pthread_mutex_unlock(&resource_lock);
            usleep(rand() % 2000); // Retry later
        }
    }
    return NULL;
}

void *coordinator_thread()
{
    while (1)
    {
        sem_wait(coord_sem);

        pthread_mutex_lock(&resource_lock);

        if (total_learners_done >= total_learners)
        {
            for (int i = 0; i < total_instructors; i++)
            {
                sem_post(instr_sem);
            }

            pthread_mutex_unlock(&resource_lock);
            pthread_exit(NULL);
        }

        if (num_learners_in_queue == 0)
        {
            pthread_mutex_unlock(&resource_lock);
            continue;
        }

        int learner_id = waiting_room_queue[0];
        num_learners_in_queue--;

        for (int i = 0; i < num_learners_in_queue; i++)
            waiting_room_queue[i] = waiting_room_queue[i + 1];

        printf("Coordinator processes Learner %d. Waiting learners = %d. Total requests = %d\n",
               learner_id, available_seats - free_seats, total_requests_to_coordinator);

        learners[learner_id].state = -3;

        sem_post(instr_sem);
        pthread_mutex_unlock(&resource_lock);
    }

    return NULL;
}

void *instructor_thread(void *arg)
{
    int instructor_id = *(int *)arg;

    while (1)
    {
        sem_wait(instr_sem);

        pthread_mutex_lock(&resource_lock);

        if (total_learners_done >= total_learners)
        {
            pthread_mutex_unlock(&resource_lock);
            pthread_exit(NULL);
        }

        int learner_id = -1;
        int least_sessions = INT_MAX;
        int earliest_arrival = INT_MAX;

        for (int i = 0; i < total_learners; i++)
        {
            if (learners[i].state == -3)
            {
                if (learners[i].sessions_received < least_sessions)
                {
                    learner_id = i;
                    least_sessions = learners[i].sessions_received;
                    earliest_arrival = learners[learner_id].time_of_arrival;
                }
                else if (learners[i].sessions_received == least_sessions && learners[learner_id].time_of_arrival < earliest_arrival)
                {
                    learner_id = i;
                    earliest_arrival = learners[i].time_of_arrival;
                }
            }
        }

        if (learner_id == -1)
        {
            pthread_mutex_unlock(&resource_lock);
            continue;
        }

        learners[learner_id].state = 0;

        free_seats++;
        current_sessions_in_progress++;

        pthread_mutex_unlock(&resource_lock);

        usleep(200); // Simulate instruction session

        pthread_mutex_lock(&resource_lock);

        learners[learner_id].sessions_received++;
        current_sessions_in_progress--;
        total_completed_sessions++;

        learner_in_session_by[learner_id] = instructor_id;

        printf("Instructor %d tutored Learner %d. Sessions in progress = %d. Total sessions completed = %d\n", instructor_id, learner_id, current_sessions_in_progress, total_completed_sessions);

        learners[learner_id].state = -1;

        pthread_mutex_unlock(&resource_lock);
    }
    return NULL;
}

int main(int argc, char *argv[])
{
    srand(time(NULL));

    total_learners = atoi(argv[1]);
    total_instructors = atoi(argv[2]);
    available_seats = atoi(argv[3]);
    session_limit = atoi(argv[4]);

    learners = (Learner *)malloc(total_learners * sizeof(Learner));
    for (int i = 0; i < total_learners; i++)
    {
        learners[i].learner_id = i;
        learners[i].sessions_received = 0;
        learners[i].state = -1;
        learners[i].time_of_arrival = INT_MAX;
    }

    instructor_ids = (int *)malloc(total_instructors * sizeof(int));
    for (int i = 0; i < total_instructors; i++)
        instructor_ids[i] = i;

    waiting_room_queue = (int *)malloc(available_seats * sizeof(int));
    num_learners_in_queue = 0;

    learner_in_session_by = (int *)malloc(total_learners * sizeof(int));
    for (int i = 0; i < total_learners; i++)
        learner_in_session_by[i] = -1;

    free_seats = available_seats;
    total_requests_to_coordinator = 0;
    current_sessions_in_progress = 0;
    total_completed_sessions = 0;
    total_learners_done = 0;
    global_clock = 0;

    coord_sem = sem_open("/coord_sem", O_CREAT, 0644, 0);
    instr_sem = sem_open("/instr_sem", O_CREAT, 0644, 0);

    pthread_mutex_init(&resource_lock, NULL);

    pthread_t learner_threads[total_learners];
    pthread_t coordinator_thread_id;
    pthread_t instructor_threads[total_instructors];

    for (int i = 0; i < total_learners; i++)
    {
        pthread_create(&learner_threads[i], NULL, learner_thread, &learners[i].learner_id);
    }

    pthread_create(&coordinator_thread_id, NULL, coordinator_thread, NULL);

    for (int i = 0; i < total_instructors; i++)
    {
        pthread_create(&instructor_threads[i], NULL, instructor_thread, &instructor_ids[i]);
    }

    for (int i = 0; i < total_learners; i++)
        pthread_join(learner_threads[i], NULL);

    pthread_join(coordinator_thread_id, NULL);

    for (int i = 0; i < total_instructors; i++)
        pthread_join(instructor_threads[i], NULL);

    pthread_mutex_destroy(&resource_lock);

    sem_close(coord_sem);
    sem_close(instr_sem);
    sem_unlink("/coord_sem");
    sem_unlink("/instr_sem");

    free(learners);
    free(instructor_ids);
    free(waiting_room_queue);
    free(learner_in_session_by);

    return 0;
}
