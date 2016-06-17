/*   Copyright 2014-2016 - Andrea Sgattoni, Luca Fedeli, Stefano Sinigardi   */

/******************************************************************************
* This file is part of piccante.                                              *
*                                                                             *
* piccante is free software: you can redistribute it and/or modify            *
* it under the terms of the GNU General Public License as published by        *
* the Free Software Foundation, either version 3 of the License, or           *
* (at your option) any later version.                                         *
*                                                                             *
* piccante is distributed in the hope that it will be useful,                 *
* but WITHOUT ANY WARRANTY; without even the implied warranty of              *
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the                *
* GNU General Public License for more details.                                *
*                                                                             *
* You should have received a copy of the GNU General Public License           *
* along with piccante. If not, see <http://www.gnu.org/licenses/>.            *
******************************************************************************/

#define _USE_MATH_DEFINES

#include <mpi.h>
#include "commons.h"
#include "grid.h"
#include "structures.h"
#include "current.h"
#include "em_field.h"
#include "particle_species.h"
#include "output_manager.h"
#include "utilities.h"
#include "jsonparser.h"

#include <cstdio>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cmath>
#include <iomanip>
#include <cstring>
#include <ctime>
#include <cstdarg>
#include <vector>
#include <map>

#define DEFAULT_DIMENSIONALITY 1


#define DIRECTORY_OUTPUT "OUTPUT"

#define RANDOM_NUMBER_GENERATOR_SEED 5489
#include "rapidjson/document.h"     // rapidjson's DOM-style API


int main(int narg, char **args)
{
  MPI_Init(&narg, &args);

  Json::Value root;
  std::string inputFileName = jsonParser::parseJsonInputFile(root, narg, args);
  int dim = jsonParser::getDimensionality(root, DEFAULT_DIMENSIONALITY);

  GRID grid(dim);
  EM_FIELD myfield;
  EM_FIELD exfield;
  CURRENT current;
  std::vector<SPECIE*> species;
  std::vector<SPECIE*>::const_iterator spec_iterator;
  my_rng_generator mt_rng;
  //*******************************************BEGIN GRID DEFINITION*******************************************************


  jsonParser::setGridGeometry(root, &grid);
  grid.mpi_grid_initialize(&narg, args);
  jsonParser::setRemainingGridParameters(root, &grid);

  srand(time(NULL));
  grid.initRNG(mt_rng, RANDOM_NUMBER_GENERATOR_SEED);

  grid.finalize();

  jsonParser::setDumpControl(root, &grid);
  grid.visualDiag();

  //********************************************END GRID DEFINITION********************************************************
  //******************** BEGIN TO READ OF user defined INPUT - PARAMETERS ****************************************
  bool isThereSpecial=false;
  bool isThereAmpli=false;
  bool isThereLambda=false;
  bool isWaveOK = false;
  double amplitude;
  double lambda;
  std::stringstream messaggio;
  Json::Value special;
  isThereSpecial=jsonParser::setValue(special,root,"special");
  if(isThereSpecial){
    isThereAmpli  = jsonParser::setDouble(&amplitude, special, "amplitude");
    isThereLambda = jsonParser::setDouble(&lambda,    special, "lambda");
    isWaveOK = isThereAmpli&&isThereLambda;
  }

  LANGMUIRset langmuirSet;

  Json::Value jsonLangmuirSet;
  langmuirSet.isThereLangmuirSet = jsonParser::setValue(jsonLangmuirSet, root, "langmuirSpectrum");

  if (langmuirSet.isThereLangmuirSet) {
    jsonParser::setDouble(&langmuirSet.refDens, jsonLangmuirSet, "refDensity");
    jsonParser::setDouble(&langmuirSet.growthRate, jsonLangmuirSet, "growthRate");
    jsonParser::setDouble(&langmuirSet.refTemp, jsonLangmuirSet, "refTemperature");
    jsonParser::setDouble(&langmuirSet.endTime, jsonLangmuirSet, "endTime");
    double amplitude=0.0;
    jsonParser::setDouble(&amplitude, jsonLangmuirSet, "amplitude");
    double centralK[3];
    centralK[0]=centralK[1]=centralK[2]=0;
    jsonParser::setDoubleArray(centralK,3,jsonLangmuirSet,"centralK");
    double sigmaK[3];
    sigmaK[0]=sigmaK[1]=sigmaK[2]=0.0;
    jsonParser::setDoubleArray(sigmaK,3,jsonLangmuirSet,"sigmaK");

    messaggio << "siamo sicuri sia tutto OK? " << " isThereLangmuirSet=" << langmuirSet.isThereLangmuirSet << std::endl;
    messaggio << "siamo sicuri sia tutto OK? " << " amplitude=" << amplitude << std::endl;
    messaggio << "siamo sicuri sia tutto OK? " << " refTemperature=" << langmuirSet.refTemp << std::endl;
    messaggio << "siamo sicuri sia tutto OK? " << " refDensity=" << langmuirSet.refDens << std::endl;
    messaggio << "siamo sicuri sia tutto OK? " << " centralKx=" << centralK[0] << std::endl;
    messaggio << "siamo sicuri sia tutto OK? " << " sigmaKx=" << sigmaK[0] << std::endl;
    messaggio << "siamo sicuri sia tutto OK? " << " endTime=" << langmuirSet.endTime << std::endl;

    UTILITIES::allocateAccessibleKModes(langmuirSet.gridModes, grid);
    UTILITIES::writeGridModes(langmuirSet.gridModes, grid);

    UTILITIES::setKModesToBeInitialised(langmuirSet.myKModes, langmuirSet.gridModes, amplitude, centralK, sigmaK);
    UTILITIES::exchangeKModesToBeInitialised(langmuirSet.myKModes, grid);
    UTILITIES::writeKModesToBeInitialised(langmuirSet.myKModes, grid);

  }
  grid.printMessage(messaggio.str());

  //********************  END READ OF "SPECIAL" (user defined) INPUT - PARAMETERS  ****************************************

  //*******************************************BEGIN SPECIES DEFINITION*********************************************************


  std::map<std::string, PLASMA*> plasmas;
  jsonParser::setPlasmas(root, plasmas);
  jsonParser::setSpecies(root, species, plasmas, &grid, mt_rng);
  UTILITIES::printTotalNumberOfParticles(species, grid);

  //*******************************************  START LANGMUIR WAVE  *********************************************************
  bool moveParticleForLangmuir=false;
  if(langmuirSet.isThereLangmuirSet&&moveParticleForLangmuir){
    int counter=0;
    for (spec_iterator = species.begin(); spec_iterator != species.end(); spec_iterator++){
      UTILITIES::moveParticles(&grid,(*spec_iterator),langmuirSet.myKModes);
      (*spec_iterator)->position_parallel_pbc();
      counter++;
    }
  }
  if(isWaveOK){
    int counter=0;
    for (spec_iterator = species.begin(); spec_iterator != species.end(); spec_iterator++){
      UTILITIES::moveParticles(&grid,(*spec_iterator),amplitude,lambda);
      (*spec_iterator)->position_parallel_pbc();
      (*spec_iterator)->position_parallel_pbc();
      counter++;
    }
  }
  //*******************************************   END  LANGMUIR WAVE  *********************************************************



  //*******************************************END SPECIES DEFINITION***********************************************************
  //*******************************************BEGIN FIELD DEFINITION*********************************************************
  myfield.allocate(&grid);
  myfield.setAllValuesToZero();

  exfield.allocate(&grid);
  exfield.setAllValuesToZero();

  current.allocate(&grid);
  current.setAllValuesToZero();

  //*******************************************    POISSON SOLVER    *********************************************************

  UTILITIES::launchPoissonSolver(myfield, species, grid, current);

  jsonParser::setLaserPulses(root, &myfield);
  myfield.boundary_conditions();


  //*******************************************END FIELD DEFINITION***********************************************************

  //*******************************************BEGIN DIAG DEFINITION**************************************************
  OUTPUT_MANAGER manager(&grid, &myfield, &current, species);
  //OUTPUT_MANAGER manager(&grid, &exfield, &current, species);

  jsonParser::setOutputManagerParameters(root, manager, species);

  manager.initialize();
  manager.copyInputFileInOutDir(inputFileName);
  //*******************************************END DIAG DEFINITION**************************************************

  //@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ MAIN CYCLE (DO NOT MODIFY) @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@

  grid.printMessage("---- START temporal cicle -----\n");

  UTILITIES::considerRestartFromDump(&grid, &myfield, species);

  while (grid.istep <= grid.getTotalNumberOfTimesteps())
  {

    grid.printTStepAsPlanned();

    manager.callDiags(grid.istep);

    myfield.openBoundariesE_1();
    myfield.new_halfadvance_B();
    myfield.boundary_conditions();

    current.setAllValuesToZero();
    for (spec_iterator = species.begin(); spec_iterator != species.end(); spec_iterator++) {
      (*spec_iterator)->current_deposition_standard(&current);
    }
    current.pbc();

    for (spec_iterator = species.begin(); spec_iterator != species.end(); spec_iterator++) {
      (*spec_iterator)->position_parallel_pbc();
    }

    myfield.openBoundariesB();
    myfield.new_advance_E(&current);

    myfield.boundary_conditions();
    myfield.openBoundariesE_2();
    myfield.new_halfadvance_B();
    myfield.boundary_conditions();

    UTILITIES::setExternaField(exfield, grid, grid.time+grid.dt, langmuirSet);
    exfield.boundary_conditions();

    for (spec_iterator = species.begin(); spec_iterator != species.end(); spec_iterator++) {
      if (grid.isRadiationFrictionEnabled()) {
        (*spec_iterator)->momenta_advance_with_friction(&myfield, grid.getLambda0());
      }
      else if(langmuirSet.isThereLangmuirSet&&langmuirSet.keepForcing){
        (*spec_iterator)->momenta_advance_with_externalFields(&myfield, &exfield);
      }
      else{
        (*spec_iterator)->momenta_advance(&myfield);
      }
    }

    grid.time += grid.dt;
    grid.istep++;
    UTILITIES::moveWindow(&grid, &myfield, species);
    UTILITIES::considerDumpForRestart(&grid, &myfield, species);
  }

  manager.close();
  MPI_Finalize();
  exit(0);
}
