#define CROW_MAIN
#define CROW_STATIC_DIR "../public"

#include "crow_all.h"
#include "json.hpp"
#include <random>

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

// Grid that contains the entities
static std::vector<std::vector<entity_t>> entity_grid;

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
        entity_grid.assign(NUM_ROWS, std::vector<entity_t>(NUM_ROWS, { empty, 0, 0}));
        
        // Instantiation of Random Number Generator Engine
        std::default_random_engine generator;
        std::uniform_int_distribution distribution(0 , (int)NUM_ROWS);

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
        
        // <YOUR CODE HERE>
        
        // Return the JSON representation of the entity grid
        nlohmann::json json_grid = entity_grid; 
        return json_grid.dump(); });
    app.port(8080).run();

    return 0;
}