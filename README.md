Fire is managed by an actor BP_FireSource. It must be placed on level. The fire is spreading in a cellular automata (i guess) fashion - there’s first fire cell, and then it just generates 8 cells around itself and burns them.

The fire source actor has some properties to control the simulation: 

![img5](https://github.com/user-attachments/assets/aa2aff3f-6d61-4807-90d8-d3d62e16cc3b)

Fire can be started by just pressing LMB, a trace will be executed which will start a fire on an
appropriate surface. You can start fire in multiple places
The fire spread depends on physical material. There are development settings in project
settings where you can set spread parameters for physical materials and exclude some
materials

![img7](https://github.com/user-attachments/assets/f24ede91-00a6-4b3d-9aba-5889c0dbc851)

Press 2 on keyboard to open UI to pause/unpause simulation, set wind direction and strength.

![img10](https://github.com/user-attachments/assets/31ebbae5-a912-4368-9b7b-3a71c151bb6c)

I’m using PCG to generate combustible items. To regenerate them one must change seed in details panel when PCG actor is selected

![img13](https://github.com/user-attachments/assets/97b5fbed-8e25-480c-b71a-c495ea4d1995)

And to change count of combustible actors one must open the PCG graph asset and change the “points per squared meter” to N / 10000 where N is the desired count of actors

![img14](https://github.com/user-attachments/assets/98d3c08d-0af1-4d4e-9290-4f1b98e5c074)

The combustible actors are BP_CombustibleCube
They have a couple of parameters, including individual combustion rate (multiplied by physics material combustion rate), color curve, max combustion level

![img17](https://github.com/user-attachments/assets/3f782741-3d7a-4c9e-8397-e5a6220129f9)

There’s wind actor on level which shows current direction of wind 
![img18](https://github.com/user-attachments/assets/9fc76f5f-bce5-455f-bfb6-9e008b7fb11c)

The wind itself is managed by UWindComponent that is placed in AFSGameState

TODOs: dynamically create box collision that would
1. envelop the fire area with the least possible amount of boxes  
2. act as nav area modifier volumes with "fire" area type so that NPCs would avoid it
3. Add evenly spreaded AIPerceptionStimuliSourceComponent scene components across the fire volume so that NPCs could see it and report it to allies
