/*************************************************
 *                                               *
 *   EXERCÍCIO COMPUTACIONAL III - ECOSIM        *
 *   HANIEL NUNES SCHIRMER CARDOSO - 2020068960  *
 *                                               *
 *************************************************/
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
#include <vector>

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
//std::mutex general_mut;
std::mutex end_task;

// Condition Variable
std::condition_variable cv;

// Custom Barrier Definition
class MyBarrier{

    private :
        std::condition_variable _cv;
        uint32_t _current_count;
        uint32_t _max_count;

    public :
        MyBarrier(uint32_t max_count) : _current_count(0) , _max_count(max_count) {};

        void wait(std::unique_lock<std::mutex>& ul){
            ++_current_count;
            while(_current_count != _max_count){
                printf("ENTROU NO MY_BARRIER\n");
                cv.wait(ul);
            }
            cv.notify_all();
            return;
        };

};

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
    int x_pos;
    int y_pos;
    entity_t(entity_type_t new_type , int32_t new_energy , int32_t new_age , int x_pos , int y_pos){
        this->type = new_type;
        this->energy = new_energy;
        this->age = new_age;
        this->x_pos = x_pos;
        this->y_pos = y_pos;
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
// Returns 0 if couldn't grow , 1 if it grew and -1 if it was killed before it could grow
int grow(entity_t* entity , int x_pos , int y_pos){

    // Instantiation of Random Number Generator Engine
    std::random_device rd;
    std::default_random_engine generator(rd());
    std::uniform_int_distribution percentage(1 , 100);

    // CRITICAL SECTION
    // No one can affect the grid while the plant grows somewhere
    std::unique_lock<std::mutex> mut(end_task);

    // Plant was killed before it could grow -> thread must now go straight into barrier
    if(entity->type != entity_type_t::plant){
        return -1;
    }

    if(percentage(generator) <= (int)(PLANT_REPRODUCTION_PROBABILITY * 100)){

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
        //std::unique_lock<std::mutex> mut(end_task);

        // Updates array with valid values
        // Essa parte aqui tá PODRÍFERA, mas vai ter que servir
        for(int x_mod = -1 ; x_mod <= 1 ; ++x_mod){
            for(int y_mod = -1 ; y_mod <= 1 ; ++y_mod){
                // Checks if Position is outside the grid
                if( ( (x_pos + x_mod) < 0 ) || ( (x_pos + x_mod) >= (int)NUM_ROWS ) 
                ||( (y_pos + y_mod) < 0 ) || ( (y_pos + y_mod) >= (int)NUM_ROWS )){
                    continue;
                }
        
                if(entity_grid.at(x_pos + x_mod).at(y_pos + y_mod).type == entity_type_t::empty){
                    pos[x_mod + 1][y_mod + 1] = true;
                    valid_count = true;
                };
            }
        }

        // If there is not a single valid position, the function returns
        if(!valid_count){
            // END OF CRITICAL SECTION
            return 0;
        }

        // Position to be acted upon
        int x_act = 0;
        int y_act = 0;
        do{
            x_act = rand_cell(generator);
            y_act = rand_cell(generator);
            printf("x_act : %d /// y_act : %d\n" , x_act , y_act);
        }while(pos[x_act + 1][y_act + 1] == false);

        entity_grid.at(x_pos + x_act).at(y_pos + y_act) = entity_t(entity_type_t::plant , 0 , 0 , (x_pos + x_act) , (y_pos + y_act));
        // END OF CRITICAL SECTION
        return 1;
    }

    // END OF CRITICAL SECTION
    return 0;

}

// Function that checks if entity needs to die (achieved maximum age possible or energy is 0)
// If it does, the entity is removed from the grid and the function returns 0
// Otherwise, the function does nothing and returns 1
// If entity was killed by another, while die() was waiting to run, returns 0 as well
int die(entity_t* entity , entity_type_t starting_type){
    // Checks Entity type and apply death rules according to each one

    // CRITICAL SECTION
    std::unique_lock<std::mutex> ul(end_task);

    if(entity->type != starting_type){
        // Entity was killed while trying to die (ironic, isn't it?), therefore, this thread needs to go straight to the barrier
        return 0;
    }

    if( ( (entity->type == entity_type_t::plant    ) && (entity->age >= PLANT_MAXIMUM_AGE    ) ) 
      ||( (entity->type == entity_type_t::herbivore) && ( (entity->age >= HERBIVORE_MAXIMUM_AGE) || (entity->energy == 0) ) )
      ||( (entity->type == entity_type_t::carnivore) && ( (entity->age >= CARNIVORE_MAXIMUM_AGE) || (entity->energy == 0) ) ) ){

        entity_grid.at(entity->x_pos).at(entity->y_pos) = entity_t(entity_type_t::empty , 0 , 0 , entity->x_pos , entity->y_pos);
        // END OF CRITICAL SECTION
        return 0;
    }
    else{
        // END OF CRITICAL SECTION
        return 1;
    }
}

// Function that updates the position of the Herbivores and Carnivores, according to each own rules
// Returns pointer to entity in new position
// If entity was killed before it could move, then the function returns a nullptr
entity_t* move(entity_t* entity , entity_type_t starting_type){

    // Instantiation of Random Number Generator Engine
    std::random_device rd;
    std::default_random_engine generator(rd());
    std::uniform_int_distribution percentage(1 , 100);

    // CRITICAL SECTION
    // No one can affect the grid while the entities are trying to move somewhere
    std::unique_lock<std::mutex> mut(end_task);

    // Entity was killed while waiting to start to move (yeah, life is truly unfair), so it returns a nullptr
    if(entity->type != starting_type){
        return nullptr;
    }

    // Entity Movements
    if( ( (entity->type == entity_type_t::herbivore) && (percentage(generator) <= (int)(HERBIVORE_MOVE_PROBABILITY * 100)) && (entity->energy >= 5))
      ||( (entity->type == entity_type_t::carnivore) && (percentage(generator) <= (int)(CARNIVORE_MOVE_PROBABILITY * 100)) && (entity->energy >= 5)) ){
        
        // Is there ate least ONE valid position?
        bool valid_count = false;

        // Array identifying each positions as valid or not
        // true == valid && false == invalid
        bool pos[3][3] = {false};

        // There's an 3x3 Grid centered on (x_pos , y_pos)
        // We can index each square of the grid based on (x_pos , y_pos) + (-1/0/1 , -1/0/1)
        std::uniform_int_distribution rand_cell(-1 , 1);

        // Updates array with valid values
        // Essa parte aqui tá PODRÍFERA, mas vai ter que servir
        for(int x_mod = -1 ; x_mod <= 1 ; ++x_mod){
            for(int y_mod = -1 ; y_mod <= 1 ; ++y_mod){
                // Checks if Position is outside the grid
                if( ( (entity->x_pos + x_mod) < 0 ) || ( (entity->x_pos + x_mod) >= (int)NUM_ROWS ) 
                  ||( (entity->y_pos + y_mod) < 0 ) || ( (entity->y_pos + y_mod) >= (int)NUM_ROWS )){
                    continue;
                }

                entity_type_t adjacent_type = entity_grid.at(entity->x_pos + x_mod).at(entity->y_pos + y_mod).type;
                if( ( (entity->type == entity_type_t::herbivore) && (adjacent_type != entity_type_t::carnivore) && (adjacent_type != entity_type_t::herbivore)) 
                  ||( (entity->type == entity_type_t::carnivore) && (adjacent_type != entity_type_t::carnivore) ) ){
                    pos[x_mod + 1][y_mod + 1] = true;
                    valid_count = true;
                };
            }
        }

        // If there is not a single valid position, the function returns
        if(!valid_count){
            // END OF CRITICAL SECTION
            // POSITION HASN'T CHANGED
            return &entity_grid.at(entity->x_pos).at(entity->y_pos);
        }

        // Position to be acted upon
        int x_act = 0;
        int y_act = 0;
        do{
            x_act = rand_cell(generator);
            y_act = rand_cell(generator);
            printf("x_act : %d /// y_act : %d\n" , x_act , y_act);
        }while(pos[x_act + 1][y_act + 1] == false);

        // Herbivore Movement & Carnivore Movement
        if(entity->type == entity_type_t::herbivore){
            entity_t& act_tile = entity_grid.at(entity->x_pos + x_act).at(entity->y_pos + y_act);
            // Tile to act upon is empty or is it occupied by a plant?
            // If it is occupied by a plant -> herbivore makes a move, spends 5 energy and eats the plant, restoring 30 energy points
            // If it is empty -> herbivore makes a move and spends 5 points of energy
            if(act_tile.type == entity_type_t::empty){
                act_tile = entity_t(entity->type , (entity->energy - 5) , entity->age , (entity->x_pos + x_act) , (entity->y_pos + y_act));
            }
            else if(act_tile.type == entity_type_t::plant){
                int32_t new_energy = (entity->energy + 25);
                if(new_energy > MAXIMUM_ENERGY){
                    new_energy = MAXIMUM_ENERGY;
                }
                act_tile = entity_t(entity->type , new_energy , entity->age , (entity->x_pos + x_act) , (entity->y_pos + y_act));
            }
        }
        else if(entity->type == entity_type_t::carnivore){
            entity_t& act_tile = entity_grid.at(entity->x_pos + x_act).at(entity->y_pos + y_act);
            // Tile to act upon is empty or is it occupied by a plant?
            // If it is empty -> carnivore makes a move and spends 5 points of energy
            // If it is occupied by a plant -> carnivore makes a move, spends 5 energy
            // If it is occupied by a herbivore -> carnivore makes a move, spends 5 energy, eats the herbivore and restores 20 energy units
            if( (act_tile.type == entity_type_t::empty) || (act_tile.type == entity_type_t::plant) ){
                act_tile = entity_t(entity->type , (entity->energy - 5) , entity->age , (entity->x_pos + x_act) , (entity->y_pos + y_act));
            }
            else if(act_tile.type == entity_type_t::herbivore){
                int32_t new_energy = (entity->energy + 15);
                if(new_energy > MAXIMUM_ENERGY){
                    new_energy = MAXIMUM_ENERGY;
                }
                act_tile = entity_t(entity->type , new_energy , entity->age , (entity->x_pos + x_act) , (entity->y_pos + y_act));
            }
        }

        entity_grid.at(entity->x_pos).at(entity->y_pos) = entity_t(entity_type_t::empty , 0 , 0 , entity->x_pos , entity->y_pos);

        // END OF CRITICAL SECTION
        // POSITION HAS CHANGED
        return &entity_grid.at(entity->x_pos + x_act).at(entity->y_pos + y_act);

    }

    // POSITION HASN'T CHANGED
    // END OF CRITICAL SECTION
    return &entity_grid.at(entity->x_pos).at(entity->y_pos);

}

// Function that implements the eating action for the Herbivores and Carnivores, according to each own rules
// Had to change entity reference to entity pointer -> due to the fact that the function needs to work with recently-moved entities
// If entity ate something, returns 1 
// If it hadn't anything to eat or the probability wasnt attended, returns 0 
// If it died before being able to eat, returns -1
int eat(entity_t* entity , entity_type_t starting_type){

    // Instantiation of Random Number Generator Engine
    std::random_device rd;
    std::default_random_engine generator(rd());
    std::uniform_int_distribution percentage(1 , 100);

    // CRITICAL SECTION
    // No one can affect the grid while entities are trying to eat
    std::unique_lock<std::mutex> mut(end_task);

    // Entity was killed before it could begin to eat -> function returns -1
    if(entity->type != starting_type){
        return -1;
    }
    
    if( ( (entity->type == entity_type_t::herbivore) && (percentage(generator) <= (int)(HERBIVORE_EAT_PROBABILITY * 100)) )
      ||( (entity->type == entity_type_t::carnivore) && (percentage(generator) <= (int)(CARNIVORE_EAT_PROBABILITY * 100)) ) ){

        // COPIEI A SEÇÃO DO CÓDIGO DENOVO NAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAO
        // Mas vai ter que ser por pressa mesmo... trágico -> pelo menos funciona né kkkk
        // Is there ate least ONE valid position?
        bool valid_count = false;

        // Array identifying each positions as valid or not
        // true == valid && false == invalid
        bool pos[3][3] = {false};

        // There's an 3x3 Grid centered on (x_pos , y_pos)
        // We can index each square of the grid based on (x_pos , y_pos) + (-1/0/1 , -1/0/1)
        std::uniform_int_distribution rand_cell(-1 , 1);

        // Updates array with valid values
        // Essa parte aqui tá PODRÍFERA, mas vai ter que servir
        for(int x_mod = -1 ; x_mod <= 1 ; ++x_mod){
            for(int y_mod = -1 ; y_mod <= 1 ; ++y_mod){
                // Checks if Position is outside the grid
                if( ( (entity->x_pos + x_mod) < 0 ) || ( (entity->x_pos + x_mod) >= (int)NUM_ROWS ) 
                    ||( (entity->y_pos + y_mod) < 0 ) || ( (entity->y_pos + y_mod) >= (int)NUM_ROWS )){
                    continue;
                }

                entity_type_t adjacent_type = entity_grid.at(entity->x_pos + x_mod).at(entity->y_pos + y_mod).type;
                if( ( (entity->type == entity_type_t::herbivore) && (adjacent_type == entity_type_t::plant) ) 
                    ||( (entity->type == entity_type_t::carnivore) && (adjacent_type == entity_type_t::herbivore) ) ){
                    pos[x_mod + 1][y_mod + 1] = true;
                    valid_count = true;
                };
            }
        }

        // If there is not a single valid position, the function returns
        if(!valid_count){
            // END OF CRITICAL SECTION
            return 0;
        }

        // Position to be acted upon
        int x_act = 0;
        int y_act = 0;
        do{
            x_act = rand_cell(generator);
            y_act = rand_cell(generator);
            printf("x_act : %d /// y_act : %d\n" , x_act , y_act);
        }while(pos[x_act + 1][y_act + 1] == false);

        // Herbivore and Carnivore actions
        if(entity->type == entity_type_t::herbivore){
            int32_t new_energy = (entity->energy + 30);
            if(new_energy > MAXIMUM_ENERGY){
                new_energy = MAXIMUM_ENERGY;
            }
            entity_grid.at(entity->x_pos + x_act).at(entity->y_pos + y_act) = entity_t(entity_type_t::empty , 0 , 0 , (entity->x_pos + x_act) , (entity->y_pos + y_act));
            entity_grid.at(entity->x_pos).at(entity->y_pos) = entity_t(entity->type , new_energy , entity->age , entity->x_pos , entity->y_pos);
        }
        else if(entity->type == entity_type_t::carnivore){
            int32_t new_energy = (entity->energy + 20);
            if(new_energy > MAXIMUM_ENERGY){
                new_energy = MAXIMUM_ENERGY;
            }
            entity_grid.at(entity->x_pos + x_act).at(entity->y_pos + y_act) = entity_t(entity_type_t::empty , 0 , 0 , (entity->x_pos + x_act) , (entity->y_pos + y_act));
            entity_grid.at(entity->x_pos).at(entity->y_pos) = entity_t(entity->type , new_energy , entity->age , entity->x_pos , entity->y_pos);
        }
        // END OF CRITICAL SECTION
        return 1;
    
    }
    
    return 0;

}

// Function that makes a copy of an entity and places it into an empty adjacent cell (if probability is met)
// If entity is copied -> returns 1
// If entity dies before being capable of redproducing -> returns -1
// If entity wasn't able to reproduce -> returns 0
int reproduce(entity_t* entity , entity_type_t starting_type){
    // Instantiation of Random Number Generator Engine
    std::random_device rd;
    std::default_random_engine generator(rd());
    std::uniform_int_distribution percentage(1 , 1000);

    // CRITICAL SECTION
    // No one can affect the grid while the entities are trying to move somewhere
    std::unique_lock<std::mutex> mut(end_task);

    // Entity was killed while waiting to start to reproduce (yeah, life is truly unfair), so it returns -1
    if(entity->type != starting_type){
        return -1;
    }

    // Entity Reproduction Measures
    if( ( (entity->type == entity_type_t::herbivore) && (percentage(generator) <= (int)(HERBIVORE_REPRODUCTION_PROBABILITY * 1000)) && (entity->energy >= THRESHOLD_ENERGY_FOR_REPRODUCTION))
      ||( (entity->type == entity_type_t::carnivore) && (percentage(generator) <= (int)(CARNIVORE_REPRODUCTION_PROBABILITY * 1000)) && (entity->energy >= THRESHOLD_ENERGY_FOR_REPRODUCTION)) ){
        
        // Is there ate least ONE valid position?
        bool valid_count = false;

        // Array identifying each positions as valid or not
        // true == valid && false == invalid
        bool pos[3][3] = {false};

        // There's an 3x3 Grid centered on (x_pos , y_pos)
        // We can index each square of the grid based on (x_pos , y_pos) + (-1/0/1 , -1/0/1)
        std::uniform_int_distribution rand_cell(-1 , 1);

        // Updates array with valid values
        // Essa parte aqui tá PODRÍFERA, mas vai ter que servir
        for(int x_mod = -1 ; x_mod <= 1 ; ++x_mod){
            for(int y_mod = -1 ; y_mod <= 1 ; ++y_mod){
                // Checks if Position is outside the grid
                if( ( (entity->x_pos + x_mod) < 0 ) || ( (entity->x_pos + x_mod) >= (int)NUM_ROWS ) 
                  ||( (entity->y_pos + y_mod) < 0 ) || ( (entity->y_pos + y_mod) >= (int)NUM_ROWS )){
                    continue;
                }

                // Checks if position is empty
                if( entity_grid.at(entity->x_pos + x_mod).at(entity->y_pos + y_mod).type == entity_type_t::empty ){
                    pos[x_mod + 1][y_mod + 1] = true;
                    valid_count = true;
                };
            }
        }

        // If there is not a single valid position, the function returns
        if(!valid_count){
            // END OF CRITICAL SECTION
            // Reproduction hasn't occured
            return 0;
        }

        // Position to be acted upon
        int x_act = 0;
        int y_act = 0;
        do{
            x_act = rand_cell(generator);
            y_act = rand_cell(generator);
            printf("x_act : %d /// y_act : %d\n" , x_act , y_act);
        }while(pos[x_act + 1][y_act + 1] == false);

        // Herbivore Reproduction & Carnivore Reproduction
        entity_grid.at(entity->x_pos + x_act).at(entity->y_pos + y_act) = entity_t(entity->type , 100 , 0 , (entity->x_pos + x_act) , (entity->y_pos + y_act));
        entity_grid.at(entity->x_pos).at(entity->y_pos) = entity_t(entity->type , (entity->energy - 10) , entity->age , entity->x_pos , entity->y_pos);

        // END OF CRITICAL SECTION
        // Reproduction has occured
        return 1;

    }

    // Reproduction didn't happen
    // END OF CRITICAL SECTION
    return 0;
}

// Function action(entity_t& , int x_pos , int y_pos)
// OBS : starting_type is used as a variable to identify if an entity was killed while trying to do something
// If entity->type != starting_type (type of entity when the thread was created) -> entity was killed and another thread now takes care of
// the entity pointed by entity (*entity) -> so, the thread needs to go straight to the barrier
int action(entity_t* entity , MyBarrier& my_barrier , const entity_type_t starting_type){

    // Instantiation of Random Number Generator Engine
    std::random_device rd;
    std::default_random_engine generator(rd());
    std::uniform_int_distribution percentage(1,3);


    printf("THREAD ENTITY %d IS EXECUTING\n" , entity->type);

    // Plant Actions
    if(entity->type == entity_type_t::plant){
        // Reproduction (if not dead)
        if( die(entity , starting_type) ){
            printf("Plant is growing\n");
            // If grow() returns -1, it means the plant died before growing, therefore, this thread needs to go straight to the barrier
            if(grow(entity , entity->x_pos , entity->y_pos) != -1){
                ++entity->age;
            }
        }
    }

    // Herbivore Actions
    if( (entity->type == entity_type_t::herbivore) || (entity->type == entity_type_t::carnivore) ){
        if(die(entity , starting_type)){

            switch((int)percentage(generator)){
                case 1:{
                    entity = move(entity , starting_type);
                    break;
                }

                case 2:{
                    eat(entity , starting_type);
                    break;
                }

                case 3:{
                    reproduce(entity , starting_type);
                    break;
                }
            }

            ++(entity->age);

            // Entity moves to a new position
            // The address of the entity in the new position is returned
            //entity = move(entity , starting_type);

            // If entity was killed by another while trying to move -> returns nullptr and thread goes straight to barrier
            // If entity was killed before it could eat anything -> returns -1 and thread goes straight into the barrier
            // If entity was killed before it could reproduce -> reuturns -1 and thread goes straight into the barrier
            /*if( (entity != nullptr) && (eat(entity , starting_type) != -1) && (reproduce(entity , starting_type) != -1)){
                ++(entity->age);
            }*/
        }
    }

    // Unique Lock with end_task mutex -> parameter to barrier
    std::unique_lock<std::mutex> ul(end_task);
    printf("Thread is terminating execution\n");
    my_barrier.wait(ul);

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
        entity_grid.assign(NUM_ROWS, std::vector<entity_t>(NUM_ROWS, { empty, 0, 0 , 0 , 0}));
        
        // Instantiation of Random Number Generator Engine
        std::random_device rd;
        std::default_random_engine generator(rd());
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
            
            }while(entity_grid.at(xgrid).at(ygrid).type != entity_type_t::empty);
            entity_grid.at(xgrid).at(ygrid) = entity_t(entity_type_t::plant , 0 , 0 , xgrid , ygrid);
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
            
            }while(entity_grid.at(xgrid).at(ygrid).type != entity_type_t::empty);
            entity_grid.at(xgrid).at(ygrid) = entity_t(entity_type_t::herbivore , 100 , 0 , xgrid , ygrid);
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
            
            }while(entity_grid.at(xgrid).at(ygrid).type != entity_type_t::empty);
            entity_grid.at(xgrid).at(ygrid) = entity_t(entity_type_t::carnivore , 100 , 0 , xgrid , ygrid);
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
                if(entity_grid.at(x).at(y).type != entity_type_t::empty){
                    ++count_thread;
                }
            }
        }

        MyBarrier my_barrier(count_thread + 1);

        // CRITICAL SECTION

        //std::vector<std::thread> thread_list;
        //thread_list.resize(count_thread);

        // unique_lock for critical section
        std::unique_lock<std::mutex> ul(end_task);

        // Now it executes every thread (QUE PORCOOOOOOOOOOOOOOOOOOOOOOOOO)
        for(int x = 0 ; x < (int)NUM_ROWS ; ++x){
            for(int y = 0 ; y < (int)NUM_ROWS ; ++y){
                if(entity_grid.at(x).at(y).type != entity_type_t::empty){
                    //thread_list.push_back(std::thread(action , std::ref(entity_grid[x][y]) , x , y , std::ref(my_barrier))) ;
                    std::thread th(action , &entity_grid[x][y] , std::ref(my_barrier) , entity_grid[x][y].type);
                    th.detach();
                }
            }
        }

        // Wait for all threads to begin execution
        // NOTE : when a new entity is born through reproduction, it can only act in the next iteration
        my_barrier.wait(ul);
        printf("MAIN FUNCTION IS TERMINATING EXECUTION!!!!!!\n");

        //thread_list.clear();
        
        // Return the JSON representation of the entity grid
        nlohmann::json json_grid = entity_grid; 
        return json_grid.dump(); });
        app.port(8080).run();
        
    return 0;
}