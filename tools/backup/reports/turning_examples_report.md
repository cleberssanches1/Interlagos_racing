# Turning Logic Comparison (Local Examples)

Scanned root: `C:/saturn/SaturnRingLib-main/Projects/Projetos_Exemplos`

## Repository Ranking

1. **vdrift-master** — score `47`
2. **stuntrally3-main** — score `30`
3. **torcs-r1-3-1** — score `24`
4. **mariokart64-master** — score `0`

## Recommended Reference

Best match: **vdrift-master**

Reason:
- Has explicit steering sign conventions.
- Contains Ackermann and yaw/slip related implementation.
- Better fit for low-speed turn startup symmetry work.

## vdrift-master

- `C:/saturn/SaturnRingLib-main/Projects/Projetos_Exemplos/vdrift-master/src/physics/cartire1.cpp` | score `12` | features `steer_sign_comment, slip_angle, pacejka`
  - `// alpha: sideslip angle is positive in a right turn(opposite to SAE tire coords)`
  - `s.slip = s.slip_angle = 0;`
- `C:/saturn/SaturnRingLib-main/Projects/Projetos_Exemplos/vdrift-master/src/physics/cartire1.h` | score `8` | features `slip_angle, pacejka`
  - `/// longitudinal force derivative at zero slip`
  - `/// pacejka magic formula parameters for longitudinal force`
- `C:/saturn/SaturnRingLib-main/Projects/Projetos_Exemplos/vdrift-master/src/physics/cartire2.h` | score `8` | features `slip_angle, pacejka`
  - `/// init peak force slip lut`
  - `btScalar coefficients[CNUM];	///< pacejka tire coefficients`
- `C:/saturn/SaturnRingLib-main/Projects/Projetos_Exemplos/vdrift-master/src/physics/cartirebase.h` | score `7` | features `steer_sign_comment, slip_angle`
  - `btScalar fy = 0; ///< positive in a right turn`
  - `/// induced lateral slip velocity from camber induced slip angle sa and lon velocity vx`
- `C:/saturn/SaturnRingLib-main/Projects/Projetos_Exemplos/vdrift-master/src/physics/cardynamics.cpp` | score `3` | features `slip_angle`
  - `cfg.get("anti-slip", d.anti_slip, error_output);`
- `C:/saturn/SaturnRingLib-main/Projects/Projetos_Exemplos/vdrift-master/src/physics/cartire2.cpp` | score `3` | features `slip_angle`
  - `s.slip = s.slip_angle = 0;`
- `C:/saturn/SaturnRingLib-main/Projects/Projetos_Exemplos/vdrift-master/src/physics/cartire3.cpp` | score `3` | features `slip_angle`
  - `s.slip = s.slip_angle = 0;`
- `C:/saturn/SaturnRingLib-main/Projects/Projetos_Exemplos/vdrift-master/src/physics/cardynamics.h` | score `3` | features `slip_angle`
  - `<< "\nSlip: " << t.slip`
- `C:/saturn/SaturnRingLib-main/Projects/Projetos_Exemplos/vdrift-master/src/physics/cartire3.h` | score `3` | features `slip_angle`
  - `/// init peak force slip lut`

## stuntrally3-main

- `C:/saturn/SaturnRingLib-main/Projects/Projetos_Exemplos/stuntrally3-main/src/vdrift/cardynamics_simulate.cpp` | score `11` | features `ackermann, steer_sign_comment, slip_angle`
  - `// ackermann stuff`
  - `//  set the steering angle,  left -1..1 right`
- `C:/saturn/SaturnRingLib-main/Projects/Projetos_Exemplos/stuntrally3-main/src/vdrift/cartire.cpp` | score `8` | features `slip_angle, pacejka`
  - `Dbl alpha = 0.0;`
  - `///  pacejka magic formula function`
- `C:/saturn/SaturnRingLib-main/Projects/Projetos_Exemplos/stuntrally3-main/src/vdrift/cartire.h` | score `8` | features `slip_angle, pacejka`
  - `Dbl Pacejka_Fy (Dbl alpha,				Dbl Fz,	Dbl gamma, Dbl friction_coeff, Dbl & maxforce_output) const;`
  - `std::vector <Dbl> longitudinal;	///< the parameters of the longitudinal pacejka equation.  this is series b`
- `C:/saturn/SaturnRingLib-main/Projects/Projetos_Exemplos/stuntrally3-main/src/vdrift/cardynamics_load.cpp` | score `3` | features `slip_angle`
  - `c.GetParamE("diff-rear.anti-slip", a);`

## torcs-r1-3-1

- `C:/saturn/SaturnRingLib-main/Projects/Projetos_Exemplos/torcs-r1-3-1/src/modules/simu/simuv2/differential.cpp` | score `3` | features `slip_angle`
  - `// Slip bias`
- `C:/saturn/SaturnRingLib-main/Projects/Projetos_Exemplos/torcs-r1-3-1/src/modules/simu/simuv2/wheel.cpp` | score `3` | features `slip_angle`
  - `tdble s, sa, sx, sy; // slip vector`
- `C:/saturn/SaturnRingLib-main/Projects/Projetos_Exemplos/torcs-r1-3-1/src/modules/simu/simuv3/aero.cpp` | score `3` | features `slip_angle`
  - `tdble alpha = 0.0f;`
- `C:/saturn/SaturnRingLib-main/Projects/Projetos_Exemplos/torcs-r1-3-1/src/modules/simu/simuv3/differential.cpp` | score `3` | features `slip_angle`
  - `// Limited slip differential with:`
- `C:/saturn/SaturnRingLib-main/Projects/Projetos_Exemplos/torcs-r1-3-1/src/modules/simu/simuv3/engine.cpp` | score `3` | features `slip_angle`
  - `tdble alpha = 0.0;`
- `C:/saturn/SaturnRingLib-main/Projects/Projetos_Exemplos/torcs-r1-3-1/src/modules/simu/simuv3/wheel.cpp` | score `3` | features `slip_angle`
  - `tdble s, sa, sx, sy; // slip vector`
- `C:/saturn/SaturnRingLib-main/Projects/Projetos_Exemplos/torcs-r1-3-1/src/modules/simu/simuv2/wheel.h` | score `3` | features `slip_angle`
  - `tdble	sa;		/* slip angle */`
- `C:/saturn/SaturnRingLib-main/Projects/Projetos_Exemplos/torcs-r1-3-1/src/modules/simu/simuv3/wheel.h` | score `3` | features `slip_angle`
  - `tdble	sa;		/* slip angle */`
- `C:/saturn/SaturnRingLib-main/Projects/Projetos_Exemplos/torcs-r1-3-1/src/modules/simu/simuv2/steer.cpp` | score `2` | features `steer_speed_limit`
  - `car->steer.maxSpeed  = GfParmGetNum(hdle, SECT_STEER, PRM_STEERSPD, (char*)NULL, 1.0f);`
- `C:/saturn/SaturnRingLib-main/Projects/Projetos_Exemplos/torcs-r1-3-1/src/modules/simu/simuv3/steer.cpp` | score `2` | features `steer_speed_limit`
  - `car->steer.maxSpeed  = GfParmGetNum(hdle, SECT_STEER, PRM_STEERSPD, (char*)NULL, 1.0);`

## mariokart64-master

_No relevant files found._
