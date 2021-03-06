#include <game/player_character.h>
#include <game/game_manager.h>
#include <game/physics_manager.h>
#include <utils/log.h>

namespace game
{
    PlayerCharacterManager::PlayerCharacterManager(core::EntityManager& entityManager, PhysicsManager& physicsManager, GameManager& gameManager) :
        ComponentManager(entityManager),
        physicsManager_(physicsManager),
        gameManager_(gameManager)

    {

    }

    void PlayerCharacterManager::FixedUpdate(sf::Time dt)
    {
        for (core::Entity playerEntity = 0; playerEntity < entityManager_.GetEntitiesSize(); playerEntity++)
        {
            if (!entityManager_.HasComponent(playerEntity, static_cast<core::EntityMask>(ComponentType::PLAYER_CHARACTER)))
                continue;

            auto playerBody = physicsManager_.GetBody(playerEntity);
            auto playerCharacter = GetComponent(playerEntity);
            const auto input = playerCharacter.input;

            //check if the player position exceed 100 in y to know if he win the game 
            if (playerBody.position.y >= 100)
            {
                auto firstPlayerEntity = gameManager_.GetEntityFromPlayerNumber(0);
                auto secondPlayerEntity = gameManager_.GetEntityFromPlayerNumber(1);

                if (playerEntity == firstPlayerEntity)
                {
                    auto player = GetComponent(firstPlayerEntity);
                    player.player1win++;
                    SetComponent(firstPlayerEntity, player);
                }
                if (playerEntity == secondPlayerEntity)
                {
                    auto player = GetComponent(secondPlayerEntity);
                    player.player2win++;
                    SetComponent(secondPlayerEntity, player);
                }
            }


            const bool right = input & PlayerInputEnum::PlayerInput::RIGHT;
            const bool left = input & PlayerInputEnum::PlayerInput::LEFT;
            const bool up = input & PlayerInputEnum::PlayerInput::UP;
            const bool down = input & PlayerInputEnum::PlayerInput::DOWN;

            const auto angularVelocity = ((left ? -1.5f : 0.0f) + (right ? 1.5f : 0.0f)) * playerAngularSpeed;

            playerBody.angularVelocity = angularVelocity;

            auto dir = core::Vec2f::up();
            dir = dir.Rotate(-(playerBody.rotation + playerBody.angularVelocity * dt.asSeconds()));

            const auto acceleration = ((down ? -1.5f : 0.0f) + (up ? 1.5f : 0.0f)) * dir;

            playerBody.velocity += acceleration * dt.asSeconds();

            //the player speed cannot exceed the maxSpeed value
            if (playerBody.velocity.GetMagnitude() >= playerCharacter.maxSpeed)
            {
                playerBody.velocity = playerBody.velocity.GetNormalized() * playerCharacter.maxSpeed;
            }
            physicsManager_.SetBoxBody(playerEntity, playerBody);

            
            /*
            if (playerCharacter.invincibilityTime > 0.0f)
            {
                playerCharacter.invincibilityTime -= dt.asSeconds();
                SetComponent(playerEntity, playerCharacter);
            }
            //Check if cannot shoot, and increase shootingTime
            if (playerCharacter.shootingTime < playerShootingPeriod)
            {
                playerCharacter.shootingTime += dt.asSeconds();
                SetComponent(playerEntity, playerCharacter);
            }
            //Shooting mechanism
            if (playerCharacter.shootingTime >= playerShootingPeriod)
            {
                if (input & PlayerInputEnum::PlayerInput::SHOOT)
                {
                    const auto currentPlayerSpeed = playerBody.velocity.GetMagnitude();
                    const auto bulletVelocity = dir *
                        ((core::Vec2f::Dot(playerBody.velocity, dir) > 0.0f ? currentPlayerSpeed : 0.0f)
                            + bulletSpeed);
                    const auto bulletPosition = playerBody.position + dir * 0.5f + playerBody.velocity * dt.asSeconds();
                    gameManager_.SpawnBullet(playerCharacter.playerNumber,
                        bulletPosition,
                        bulletVelocity);
                    playerCharacter.shootingTime = 0.0f;
                    SetComponent(playerEntity, playerCharacter);
                }
            }*/
        }

        
    }
}
