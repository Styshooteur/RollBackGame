#include <game/rollback_manager.h>
#include <game/game_manager.h>
#include <cassert>
#include <utils/log.h>
#include <fmt/format.h>

namespace game
{

    RollbackManager::RollbackManager(GameManager& gameManager, core::EntityManager& entityManager) :
        gameManager_(gameManager), entityManager_(entityManager),
        currentTransformManager_(entityManager),
        currentPhysicsManager_(entityManager), currentPlayerManager_(entityManager, currentPhysicsManager_, gameManager_),
        lastValidatePhysicsManager_(entityManager),
        lastValidatePlayerManager_(entityManager, lastValidatePhysicsManager_, gameManager_),
        boxBodyManager_(entityManager)
    {
        for (auto& input : inputs_)
        {
            std::fill(input.begin(), input.end(), 0u);
        }
        currentPhysicsManager_.RegisterTriggerListener(*this);
    }

    void RollbackManager::SimulateToCurrentFrame()
    {
        const auto currentFrame = gameManager_.GetCurrentFrame();
        const auto lastValidateFrame = gameManager_.GetLastValidateFrame();
        //Destroying all created Entities after the last validated frame
        for (const auto& createdEntity : createdEntities_)
        {
            if (createdEntity.createdFrame > lastValidateFrame)
            {
                entityManager_.DestroyEntity(createdEntity.entity);
            }
        }
        createdEntities_.clear();
        //Remove DESTROY flags
        for (core::Entity entity = 0; entity < entityManager_.GetEntitiesSize(); entity++)
        {
            if (entityManager_.HasComponent(entity, static_cast<core::EntityMask>(ComponentType::DESTROYED)))
            {
                entityManager_.RemoveComponent(entity, static_cast<core::EntityMask>(ComponentType::DESTROYED));
            }
        }
        
        //Revert the current game state to the last validated game state
        currentPhysicsManager_.CopyAllComponents(lastValidatePhysicsManager_);
        currentPlayerManager_.CopyAllComponents(lastValidatePlayerManager_.GetAllComponents());

        for (Frame frame = lastValidateFrame + 1; frame <= currentFrame; frame++)
        {
            testedFrame_ = frame;
            //Copy player inputs to player manager
            for (PlayerNumber playerNumber = 0; playerNumber < maxPlayerNmb; playerNumber++)
            {
                const auto playerInput = GetInputAtFrame(playerNumber, frame);
                const auto playerEntity = gameManager_.GetEntityFromPlayerNumber(playerNumber);
                if(playerEntity == core::EntityManager::INVALID_ENTITY)
                {
                    core::LogWarning(fmt::format("Invalid Entity in {}:line {}", __FILE__, __LINE__));
                    continue;
                }
                auto playerCharacter = currentPlayerManager_.GetComponent(playerEntity);
                playerCharacter.input = playerInput;
                currentPlayerManager_.SetComponent(playerEntity, playerCharacter);
            }
            //Simulate one frame of the game
            currentPlayerManager_.FixedUpdate(sf::seconds(GameManager::FixedPeriod));
            currentPhysicsManager_.FixedUpdate(sf::seconds(GameManager::FixedPeriod));
        }
        //Copy the physics states to the transforms
        for (core::Entity entity = 0; entity < entityManager_.GetEntitiesSize(); entity++)
        {
            if (!entityManager_.HasComponent(entity,
                                             static_cast<core::EntityMask>(core::ComponentType::BOXBODY2D) |
                                             static_cast<core::EntityMask>(core::ComponentType::TRANSFORM)))
                continue;
            const auto& body = currentPhysicsManager_.GetBody(entity);
            currentTransformManager_.SetPosition(entity, body.position);
            currentTransformManager_.SetRotation(entity, body.rotation);
        }
    }
    void RollbackManager::SetPlayerInput(PlayerNumber playerNumber, PlayerInput playerInput, std::uint32_t inputFrame)
    {
        //Should only be called on the server
        if (currentFrame_ < inputFrame)
        {
            StartNewFrame(inputFrame);
        }
        inputs_[playerNumber][currentFrame_ - inputFrame] = playerInput;
        if (lastReceivedFrame_[playerNumber] < inputFrame)
        {
            lastReceivedFrame_[playerNumber] = inputFrame;
            //Repeat the same inputs until currentFrame
            for (size_t i = 0; i < currentFrame_ - inputFrame; i++)
            {
                inputs_[playerNumber][i] = playerInput;
            }
        }
    }

    void RollbackManager::StartNewFrame(Frame newFrame)
    {
        if (currentFrame_ > newFrame)
            return;
        const auto delta = newFrame - currentFrame_;
        if (delta == 0)
        {
            return;
        }
        for (auto& inputs : inputs_)
        {
            for (auto i = inputs.size() - 1; i >= delta; i--)
            {
                inputs[i] = inputs[i - delta];
            }

            for (Frame i = 0; i < delta; i++)
            {
                inputs[i] = inputs[delta];
            }
        }
        currentFrame_ = newFrame;
    }

    void RollbackManager::ValidateFrame(Frame newValidateFrame)
    {
        const auto lastValidateFrame = gameManager_.GetLastValidateFrame();
        //Destroying all created Entities after the last validated frame
        for (const auto& createdEntity : createdEntities_)
        {
            if (createdEntity.createdFrame > lastValidateFrame)
            {
                entityManager_.DestroyEntity(createdEntity.entity);
            }
        }
        createdEntities_.clear();
        //Remove DESTROYED flag
        for (core::Entity entity = 0; entity < entityManager_.GetEntitiesSize(); entity++)
        {
            if (entityManager_.HasComponent(entity, static_cast<core::EntityMask>(ComponentType::DESTROYED)))
            {
                entityManager_.RemoveComponent(entity, static_cast<core::EntityMask>(ComponentType::DESTROYED));
            }

        }
        createdEntities_.clear();
        //We check that we got all the inputs
        for (PlayerNumber playerNumber = 0; playerNumber < maxPlayerNmb; playerNumber++)
        {
            if (GetLastReceivedFrame(playerNumber) < newValidateFrame)
            {
                assert(false && "We should not validate a frame if we did not receive all inputs!!!");
                return;
            }
        }
        //We use the current game state as the temporary new validate game state
        currentPhysicsManager_.CopyAllComponents(lastValidatePhysicsManager_);
        currentPlayerManager_.CopyAllComponents(lastValidatePlayerManager_.GetAllComponents());

        //We simulate the frames until the new validated frame
        for (Frame frame = lastValidateFrame_ + 1; frame <= newValidateFrame; frame++)
        {
            testedFrame_ = frame;
            //Copy the players inputs into the player manager
            for (PlayerNumber playerNumber = 0; playerNumber < maxPlayerNmb; playerNumber++)
            {
                const auto playerInput = GetInputAtFrame(playerNumber, frame);
                const auto playerEntity = gameManager_.GetEntityFromPlayerNumber(playerNumber);
                auto playerCharacter = currentPlayerManager_.GetComponent(playerEntity);
                playerCharacter.input = playerInput;
                currentPlayerManager_.SetComponent(playerEntity, playerCharacter);
            }
            //We simulate one frame
            currentPlayerManager_.FixedUpdate(sf::seconds(GameManager::FixedPeriod));
            currentPhysicsManager_.FixedUpdate(sf::seconds(GameManager::FixedPeriod));
        }
        //Definitely remove DESTROY entities
        for (core::Entity entity = 0; entity < entityManager_.GetEntitiesSize(); entity++)
        {
            if (entityManager_.HasComponent(entity, static_cast<core::EntityMask>(ComponentType::DESTROYED)))
            {
                entityManager_.DestroyEntity(entity);
            }
        }
        //Copy back the new validate game state to the last validated game state
        lastValidatePlayerManager_.CopyAllComponents(currentPlayerManager_.GetAllComponents());
        lastValidatePhysicsManager_.CopyAllComponents(currentPhysicsManager_);
        lastValidateFrame_ = newValidateFrame;
        createdEntities_.clear();
    }
    void RollbackManager::ConfirmFrame(Frame newValidateFrame, const std::array<PhysicsState, maxPlayerNmb>& serverPhysicsState)
    {
        ValidateFrame(newValidateFrame);
        for (PlayerNumber playerNumber = 0; playerNumber < maxPlayerNmb; playerNumber++)
        {
            const PhysicsState lastPhysicsState = GetValidatePhysicsState(playerNumber);
            if (serverPhysicsState[playerNumber] != lastPhysicsState)
            {
                assert(false && "Physics State are not equal");
            }
        }
    }
    PhysicsState RollbackManager::GetValidatePhysicsState(PlayerNumber playerNumber) const
    {
        PhysicsState state = 0;
        const core::Entity playerEntity = gameManager_.GetEntityFromPlayerNumber(playerNumber);
        const auto& playerBody = lastValidatePhysicsManager_.GetBody(playerEntity);

        const auto pos = playerBody.position;
        const auto* posPtr = reinterpret_cast<const PhysicsState*>(&pos);
        //Adding position
        for (size_t i = 0; i < sizeof(core::Vec2f) / sizeof(PhysicsState); i++)
        {
            state += posPtr[i];
        }

        //Adding velocity
        const auto velocity = playerBody.velocity;
        const auto* velocityPtr = reinterpret_cast<const PhysicsState*>(&velocity);
        for (size_t i = 0; i < sizeof(core::Vec2f) / sizeof(PhysicsState); i++)
        {
            state += velocityPtr[i];
        }
        //Adding rotation
        const auto angle = playerBody.rotation.value();
        const auto* anglePtr = reinterpret_cast<const PhysicsState*>(&angle);
        for (size_t i = 0; i < sizeof(float) / sizeof(PhysicsState); i++)
        {
            state += anglePtr[i];
        }
        //Adding angular Velocity
        const auto angularVelocity = playerBody.angularVelocity.value();
        const auto* angularVelPtr = reinterpret_cast<const PhysicsState*>(&angularVelocity);
        for (size_t i = 0; i < sizeof(float) / sizeof(PhysicsState); i++)
        {
            state += angularVelPtr[i];
        }
        return state;
    }

    void RollbackManager::SpawnPlayer(PlayerNumber playerNumber, core::Entity entity, core::Vec2f position, core::degree_t rotation)
    {
        BoxBody playerBoxBody;
        playerBoxBody.position = position;
        playerBoxBody.rotation = rotation;
        playerBoxBody.extends = core::Vec2f(0.64, 0.55) / 2;

        PlayerCharacter playerCharacter;
        playerCharacter.playerNumber = playerNumber;

        currentPlayerManager_.AddComponent(entity);
        currentPlayerManager_.SetComponent(entity, playerCharacter);

        currentPhysicsManager_.AddBoxBody(entity);
        currentPhysicsManager_.SetBoxBody(entity, playerBoxBody);
       
        lastValidatePlayerManager_.AddComponent(entity);
        lastValidatePlayerManager_.SetComponent(entity, playerCharacter);

        lastValidatePhysicsManager_.AddBoxBody(entity);
        lastValidatePhysicsManager_.SetBoxBody(entity, playerBoxBody);
      
        currentTransformManager_.AddComponent(entity);
        currentTransformManager_.SetPosition(entity, position);
        currentTransformManager_.SetRotation(entity, rotation);
    }

    void RollbackManager::SpawnBox(core::Entity entity, core::Vec2f position)
    {
        BoxBody boxBoxBody;
        boxBoxBody.position = position;
        boxBoxBody.extends = core::Vec2f(1.28, 0.32) / 2;
        boxBoxBody.bodyType = BodyType::STATIC;
        
       
        currentPhysicsManager_.AddBoxBody(entity);
        currentPhysicsManager_.SetBoxBody(entity, boxBoxBody);

        lastValidatePhysicsManager_.AddBoxBody(entity);
        lastValidatePhysicsManager_.SetBoxBody(entity, boxBoxBody);

        currentTransformManager_.AddComponent(entity);
        currentTransformManager_.SetPosition(entity, position);
        
    }

    void RollbackManager::SpawnFlag(core::Entity entity, core::Vec2f position)
    {
        BoxBody flagBoxBody;
        flagBoxBody.position = position;
        flagBoxBody.bodyType = BodyType::STATIC;
        

        currentTransformManager_.AddComponent(entity);
        currentTransformManager_.SetPosition(entity, position);
    }

    void RollbackManager::SpawnTrack(core::Entity entity, core::Vec2f position)
    {
        BoxBody trackBoxBody;
        trackBoxBody.position = position;
        trackBoxBody.bodyType = BodyType::STATIC;
        

        currentTransformManager_.AddComponent(entity);
        currentTransformManager_.SetPosition(entity, position);
    }

    void RollbackManager::SpawnWall(core::Entity entity, core::Vec2f position)
    {
        BoxBody wallBoxBody;
        wallBoxBody.position = position;
        wallBoxBody.extends = core::Vec2f(0.32, 110) / 2;
        wallBoxBody.bodyType = BodyType::STATIC;

        currentPhysicsManager_.AddBoxBody(entity);
        currentPhysicsManager_.SetBoxBody(entity, wallBoxBody);

        lastValidatePhysicsManager_.AddBoxBody(entity);
        lastValidatePhysicsManager_.SetBoxBody(entity, wallBoxBody);

        currentTransformManager_.AddComponent(entity);
        currentTransformManager_.SetPosition(entity, position);
    }

    void RollbackManager::SpawnGreatBox(core::Entity entity, core::Vec2f position)
    {
        BoxBody greatBoxBoxBody;
        greatBoxBoxBody.position = position;
        greatBoxBoxBody.extends = core::Vec2f(3.84, 0.32) / 2;
        greatBoxBoxBody.bodyType = BodyType::STATIC;


        currentPhysicsManager_.AddBoxBody(entity);
        currentPhysicsManager_.SetBoxBody(entity, greatBoxBoxBody);

        lastValidatePhysicsManager_.AddBoxBody(entity);
        lastValidatePhysicsManager_.SetBoxBody(entity, greatBoxBoxBody);

        currentTransformManager_.AddComponent(entity);
        currentTransformManager_.SetPosition(entity, position);
    }

    PlayerInput RollbackManager::GetInputAtFrame(PlayerNumber playerNumber, Frame frame)
    {
        assert(currentFrame_ - frame < inputs_[playerNumber].size() &&
            "Trying to get input too far in the past");
        return inputs_[playerNumber][currentFrame_ - frame];
    }


    void RollbackManager::DestroyEntity(core::Entity entity)
    {
        //we don't need to save a bullet that has been created in the time window
        if (std::find_if(createdEntities_.begin(), createdEntities_.end(), [entity](auto newEntity)
            {
                return newEntity.entity == entity;
            }) != createdEntities_.end())
        {
            entityManager_.DestroyEntity(entity);
            return;
        }
        entityManager_.AddComponent(entity, static_cast<core::EntityMask>(ComponentType::DESTROYED));
    }

    void RollbackManager::OnTrigger(core::Entity entity1, core::Entity entity2)
    {
    }
}
