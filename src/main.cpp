#define CROW_MAIN
#define CROW_STATIC_DIR "../public"

#include "crow_all.h"
#include "json.hpp"
#include <random>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <barrier>
#include <functional>
#include <cstdio>

static const uint32_t NUM_ROWS = 15;

// Constants
const uint32_t PLANT_MAXIMUM_AGE = 10;
const uint32_t HERBIVORE_MAXIMUM_AGE = 50;
const uint32_t CARNIVORE_MAXIMUM_AGE = 80;
const uint32_t MAXIMUM_ENERGY = 200;
const uint32_t THRESHOLD_ENERGY_FOR_REPRODUCTION = 20;

// Probabilities
const double PLANT_REPRODUCTION_PROBABILITY = 0.2;
const double HERBIVORE_REPRODUCTION_PROBABILITY = 0.075;
const double CARNIVORE_REPRODUCTION_PROBABILITY = 0.025;
const double HERBIVORE_MOVE_PROBABILITY = 0.7;
const double HERBIVORE_EAT_PROBABILITY = 0.9;
const double CARNIVORE_MOVE_PROBABILITY = 0.5;
const double CARNIVORE_EAT_PROBABILITY = 1.0;

// Mutexes
std::mutex general_mut;
std::mutex end_task;

// Condition Variable
std::condition_variable cv;

// Pointer to Barrier
auto my_barrier = new std::barrier(0);

// Type definitions
enum entity_type_t
{
    empty,
    plant,
    herbivore,
    carnivore
};

struct pos_t
{
    uint32_t i;
    uint32_t j;
};

struct entity_t
{
    entity_type_t type;
    int32_t energy;
    int32_t age;
    entity_t(entity_type_t new_type , int32_t new_energy , int32_t new_age){
        this->type = new_type;
        this->energy = new_energy;
        this->age = new_age;
    };
};

// Grid that contains the entities
static std::vector<std::vector<entity_t>> entity_grid;

// Auxiliary code to convert the entity_type_t enum to a string
NLOHMANN_JSON_SERIALIZE_ENUM(entity_type_t, {
                                                {empty, " "},
                                                {plant, "P"},
                                                {herbivore, "H"},
                                                {carnivore, "C"},
                                            })

// Auxiliary code to convert the entity_t struct to a JSON object
namespace nlohmann
{
    void to_json(nlohmann::json &j, const entity_t &e)
    {
        j = nlohmann::json{{"type", e.type}, {"energy", e.energy}, {"age", e.age}};
    }
}

// Function that makes the Plant grow into a valid position
// A valid position is inside the grid and is not currently occupied by some other entity
// Com certeza tem um jeito mais eficiente de fazer isso mas fazer oq né socorro
int grow(int x_pos , int y_pos , std::default_random_engine generator){

    // Is there ate least ONE valid position?
    bool valid_count = false;

    // Array identifying each positions as valid or not
    // true == valid && false == invalid
    bool pos[3][3] = {false};

    // There's an 3x3 Grid centered on (x_pos , y_pos)
    // We can index each square of the grid based on (x_pos , y_pos) + (-1/0/1 , -1/0/1)
    std::uniform_int_distribution rand_cell(-1 , 1);

    // CRITICAL SECTION
    // No one can affect the grid while the plant grows somewhere
    general_mut.lock();

    // Updates array with valid values
    // Essa parte aqui tá PODRÍFERA, mas vai ter que servir
    for(int x_mod = -1 ; x_mod < 1 ; ++x_mod){
        for(int y_mod = -1 ; y_mod < 1 ; ++y_mod){
            // Checks if Position is outside the grid
            if( ( (x_pos + x_mod) < 0 ) || ( (x_pos + x_mod) >= (int)NUM_ROWS ) 
              ||( (y_pos + y_mod) < 0 ) || ( (y_pos + y_mod) >= (int)NUM_ROWS )){
                continue;
            }
    
            if(entity_grid[x_pos + x_mod][y_pos + y_mod].type == entity_type_t::empty){
                pos[x_mod + 1][y_mod + 1] = true;
                valid_count = true;
            };
        }
    }

    // If there is not a single valid position, the function returns
    if(!valid_count){
        general_mut.unlock();
        // END OF CRITICAL SECTION
        return -1;
    }

    // Position to be acted upon
    int x_act = 0;
    int y_act = 0;
    do{
        x_act = rand_cell(generator);
        y_act = rand_cell(generator);
    }while(pos[x_act + 1][y_act + 1] == false);

    entity_grid[x_pos + x_act][y_pos + y_act] = entity_t(entity_type_t::plant , 0 , 0);
    fprintf(stdout , "UMA NOVA PLANTA NASCEEEEEU\n");

    general_mut.unlock();
    // END OF CRITICAL SECTION

    return 0;
}

// Function action(entity_t& , int x_pos , int y_pos)
int action(entity_t& entity , int x_pos , int y_pos){
    // Instantiation of Random Number Generator Engine
    std::default_random_engine generator;
    std::uniform_int_distribution percentage(1 , 100);

    // Cell Positions to be Acted Upon

    // Plant Actions
    if(entity.type == entity_type_t::plant){
        // Reproduction
        if(percentage(generator) <= 20){
            grow(x_pos , y_pos , generator);
            fprintf(stdout , "PLANTA SUPREMACIAAA\n");
        }
    }

    my_barrier->arrive_and_wait();

    return 0;
}

int main()
{
    crow::SimpleApp app;

    // Endpoint to serve the HTML page
    CROW_ROUTE(app, "/")
    ([](crow::request &, crow::response &res)
     {
        // Return the HTML content here
        res.set_static_file_info_unsafe("../public/index.html");
        res.end(); });

    CROW_ROUTE(app, "/start-simulation")
        .methods("POST"_method)([](crow::request &req, crow::response &res)
                                { 
        // Parse the JSON request body
        nlohmann::json request_body = nlohmann::json::parse(req.body);

        // Extract the quantity of entities per type
        uint32_t num_plants = (uint32_t)request_body["plants"];
        uint32_t num_herbivores = (uint32_t)request_body["herbivores"];
        uint32_t num_carnivores = (uint32_t)request_body["carnivores"];
        
        printf("NUMERO DE PLANTAS : %d\n" , num_plants);
        printf("NUMERO DE HERBIVORES : %d\n" , num_herbivores);
        printf("NUMERO DE CARNIVORES : %d\n" , num_carnivores);
        
        // Validate the request body 
        uint32_t total_entinties = num_plants + num_herbivores + num_carnivores;
        if (total_entinties > NUM_ROWS * NUM_ROWS) {
        res.code = 400;
        res.body = "Too many entities";
        res.end();
        return;
        }

        // Clear the entity grid
        entity_grid.clear();
        entity_grid.assign(NUM_ROWS, std::vector<entity_t>(NUM_ROWS, { empty, 0, 0}));
        
        // Instantiation of Random Number Generator Engine
        std::default_random_engine generator;
        std::uniform_int_distribution distribution(0 , (int)(NUM_ROWS - 1));

        // Create the entities
        // Create Plants
        for(int amount = 0 ; amount < num_plants ; ++amount){
            // X Horizontal && Y Vertical
            int xgrid = 0;
            int ygrid = 0;
            do
            {
                xgrid = distribution(generator);
                ygrid = distribution(generator);
            
            }while(entity_grid[xgrid][ygrid].type != entity_type_t::empty);
            entity_grid[xgrid][ygrid] = entity_t(entity_type_t::plant , 0 , 0);
        }
        // Create Herbivores
        for(int amount = 0 ; amount < num_herbivores ; ++amount){
            // X Horizontal && Y Vertical
            int xgrid = 0;
            int ygrid = 0;
            do
            {
                xgrid = distribution(generator);
                ygrid = distribution(generator);
            
            }while(entity_grid[xgrid][ygrid].type != entity_type_t::empty);
            entity_grid[xgrid][ygrid] = entity_t(entity_type_t::herbivore , 100 , 0);
        }
        // Create Carnivores
        for(int amount = 0 ; amount < num_carnivores ; ++amount){
            // X Horizontal && Y Vertical
            int xgrid = 0;
            int ygrid = 0;
            do
            {
                xgrid = distribution(generator);
                ygrid = distribution(generator);
            
            }while(entity_grid[xgrid][ygrid].type != entity_type_t::empty);
            entity_grid[xgrid][ygrid] = entity_t(entity_type_t::carnivore , 100 , 0);
        }


        // Return the JSON representation of the entity grid
        nlohmann::json json_grid = entity_grid; 
        res.body = json_grid.dump();
        res.end(); });

    // Endpoint to process HTTP GET requests for the next simulation iteration
    CROW_ROUTE(app, "/next-iteration")
        .methods("GET"_method)([]()
                               {
        // Simulate the next iteration
        // Iterate over the entity grid and simulate the behaviour of each entity

        // Count the number of threads to be executed
        int count_thread = 0;
        for(int x = 0 ; x < (int)NUM_ROWS ; ++x){
            for(int y = 0 ; y < (int)NUM_ROWS ; ++y){
                if(entity_grid[x][y].type != entity_type_t::empty){
                    ++count_thread;
                }
            }
        }

        // Barrier -> main() waits for every entity to execute
        free(my_barrier);
        my_barrier = new std::barrier(count_thread + 1);

        // Now it executes every thread (QUE PORCOOOOOOOOOOOOOOOOOOOOOOOOO)
        for(int x = 0 ; x < (int)NUM_ROWS ; ++x){
            for(int y = 0 ; y < (int)NUM_ROWS ; ++y){
                if(entity_grid[x][y].type != entity_type_t::empty){
                    std::thread th(action , std::ref(entity_grid[x][y]) , x , y) ;
                    th.detach();
                }
            }
        }

        // Wait for 
        my_barrier->arrive_and_wait();
        free(my_barrier);
        
        // Return the JSON representation of the entity grid
        nlohmann::json json_grid = entity_grid; 
        return json_grid.dump(); });
        app.port(8080).run();
        
    return 0;
}