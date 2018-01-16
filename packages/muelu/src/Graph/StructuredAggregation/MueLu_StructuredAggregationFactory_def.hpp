// @HEADER
//
// ***********************************************************************
//
//        MueLu: A package for multigrid based preconditioning
//                  Copyright 2012 Sandia Corporation
//
// Under the terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
// the U.S. Government retains certain rights in this software.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the Corporation nor the names of the
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY SANDIA CORPORATION "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL SANDIA CORPORATION OR THE
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Questions? Contact
//                    Jonathan Hu       (jhu@sandia.gov)
//                    Andrey Prokopenko (aprokop@sandia.gov)
//                    Ray Tuminaro      (rstumin@sandia.gov)
//
// ***********************************************************************
//
// @HEADER
#ifndef MUELU_STRUCTUREDAGGREGATIONFACTORY_DEF_HPP_
#define MUELU_STRUCTUREDAGGREGATIONFACTORY_DEF_HPP_

#include <Xpetra_Matrix.hpp>
#include <Xpetra_Map.hpp>
#include <Xpetra_Vector.hpp>
#include <Xpetra_MultiVectorFactory.hpp>
#include <Xpetra_VectorFactory.hpp>

#include "MueLu_StructuredAggregationFactory_decl.hpp"

#include "MueLu_OnePtAggregationAlgorithm.hpp"

#include "MueLu_Level.hpp"
#include "MueLu_GraphBase.hpp"
#include "MueLu_Aggregates.hpp"
#include "MueLu_MasterList.hpp"
#include "MueLu_Monitor.hpp"
#include "MueLu_Utilities.hpp"

namespace MueLu {

  template <class Scalar, class LocalOrdinal, class GlobalOrdinal, class Node>
  StructuredAggregationFactory<Scalar, LocalOrdinal, GlobalOrdinal, Node>::
  StructuredAggregationFactory() : bDefinitionPhase_(true)
  { }

  template <class Scalar, class LocalOrdinal, class GlobalOrdinal, class Node>
  RCP<const ParameterList> StructuredAggregationFactory<Scalar, LocalOrdinal, GlobalOrdinal, Node>::
  GetValidParameterList() const {
    RCP<ParameterList> validParamList = rcp(new ParameterList());

    typedef Teuchos::StringToIntegralParameterEntryValidator<int> validatorType;
#define SET_VALID_ENTRY(name) validParamList->setEntry(name, MasterList::getEntry(name))
    SET_VALID_ENTRY("aggregation: preserve Dirichlet points");
    SET_VALID_ENTRY("aggregation: allow user-specified singletons");
    SET_VALID_ENTRY("aggregation: error on nodes with no on-rank neighbors");
#undef  SET_VALID_ENTRY

    // general variables needed in AggregationFactory
    validParamList->set<RCP<const FactoryBase> >("Coordinates",             Teuchos::null,
                                                 "Generating factory of problem coordinates");
    validParamList->set<int>                    ("aggregation: number of spatial dimensions", 3,
                                                  "The number of spatial dimensions in the problem");
    validParamList->set<std::string>            ("aggregation: coarsening rate", "{3}",
                                                  "Coarsening rate per spatial dimensions");
    validParamList->set<RCP<const FactoryBase> >("gNodesPerDim",            Teuchos::null,
                                                 "Number of nodes per spatial dimmension provided by CoordinatesTransferFactory.");
    validParamList->set<RCP<const FactoryBase> >("lNodesPerDim",            Teuchos::null,
                                                 "Number of nodes per spatial dimmension provided by CoordinatesTransferFactory.");
    validParamList->set<std::string >           ("meshLayout",              "Global Lexicographic",
                                                 "Type of mesh ordering");
    validParamList->set<RCP<const FactoryBase> >("meshData",                Teuchos::null,
                                                 "Mesh ordering associated data");

    // special variables necessary for OnePtAggregationAlgorithm
    validParamList->set<std::string>            ("OnePt aggregate map name",         "",
                                                 "Name of input map for single node aggregates. (default='')");
    validParamList->set<std::string>            ("OnePt aggregate map factory",      "",
                                                 "Generating factory of (DOF) map for single node aggregates.");

    return validParamList;
  }

  template <class Scalar, class LocalOrdinal, class GlobalOrdinal, class Node>
  void StructuredAggregationFactory<Scalar, LocalOrdinal, GlobalOrdinal, Node>::
  DeclareInput(Level& currentLevel) const {
    Input(currentLevel, "Coordinates");

    // Request the global number of nodes per dimensions
    if(currentLevel.GetLevelID() == 0) {
      if(currentLevel.IsAvailable("gNodesPerDim", NoFactory::get())) {
        currentLevel.DeclareInput("gNodesPerDim", NoFactory::get(), this);
      } else {
        TEUCHOS_TEST_FOR_EXCEPTION(currentLevel.IsAvailable("gNodesPerDim", NoFactory::get()),
                                   Exceptions::RuntimeError,
                                   "gNodesPerDim was not provided by the user on level0!");
      }
    } else {
      Input(currentLevel, "gNodesPerDim");
    }

    // Request the local number of nodes per dimensions
    if(currentLevel.GetLevelID() == 0) {
      if(currentLevel.IsAvailable("lNodesPerDim", NoFactory::get())) {
        currentLevel.DeclareInput("lNodesPerDim", NoFactory::get(), this);
      } else {
        TEUCHOS_TEST_FOR_EXCEPTION(currentLevel.IsAvailable("lNodesPerDim", NoFactory::get()),
                                   Exceptions::RuntimeError,
                                   "lNodesPerDim was not provided by the user on level0!");
      }
    } else {
      Input(currentLevel, "lNodesPerDim");
    }

    const ParameterList& pL = GetParameterList();

    // request special data necessary for OnePtAggregationAlgorithm
    std::string mapOnePtName = pL.get<std::string>("OnePt aggregate map name");
    if (mapOnePtName.length() > 0) {
      std::string mapOnePtFactName = pL.get<std::string>("OnePt aggregate map factory");
      if (mapOnePtFactName == "" || mapOnePtFactName == "NoFactory") {
        currentLevel.DeclareInput(mapOnePtName, NoFactory::get());
      } else {
        RCP<const FactoryBase> mapOnePtFact = GetFactory(mapOnePtFactName);
        currentLevel.DeclareInput(mapOnePtName, mapOnePtFact.get());
      }
    }
  }

  template <class Scalar, class LocalOrdinal, class GlobalOrdinal, class Node>
  void StructuredAggregationFactory<Scalar, LocalOrdinal, GlobalOrdinal, Node>::
  Build(Level &currentLevel) const {
    FactoryMonitor m(*this, "Build", currentLevel);

    ParameterList pL = GetParameterList();
    bDefinitionPhase_ = false;  // definition phase is finished, now all aggregation algorithm information is fixed

    // General problem informations are gathered from data stored in the problem matix.
    typedef typename Xpetra::MultiVector<double, LO, GO, NO> xdMV;
    RCP<const xdMV> Coordinates = Get< RCP<const xdMV> >(currentLevel, "Coordinates");
    TEUCHOS_TEST_FOR_EXCEPTION(Coordinates == Teuchos::null, Exceptions::RuntimeError,
                               "Coordinates cannot be accessed from fine level!");
    RCP<const Map> coordMap     = Coordinates->getMap();

    // Since we want to operate on nodes and not dof, we need to modify the rowMap in order to
    // obtain a nodeMap.
    GO numGlobalNodes = as<GO>(coordMap->getGlobalNumElements());
    LO numLocalNodes  = as<LO>(coordMap->getNodeNumElements());
    RCP<GeometricData> geoData = rcp(new GeometricData{});
    geoData->meshLayout = pL.get<std::string>("meshLayout");
    geoData->numDimensions = Coordinates->getNumVectors();
    if(currentLevel.GetLevelID() == 0) {
      // On level 0, data is provided by applications and has no associated factory.
      geoData->gFineNodesPerDir = currentLevel.Get<Array<GO> >("gNodesPerDim", NoFactory::get());
      geoData->lFineNodesPerDir = currentLevel.Get<Array<LO> >("lNodesPerDim", NoFactory::get());
    } else {
      // On level > 0, data is provided directly by generating factories.
      geoData->gFineNodesPerDir = Get<Array<GO> >(currentLevel, "gNodesPerDim");
      geoData->lFineNodesPerDir = Get<Array<LO> >(currentLevel, "lNodesPerDim");
    }
    geoData->gNumFineNodes10 = geoData->gFineNodesPerDir[1]*geoData->gFineNodesPerDir[0];
    geoData->gNumFineNodes   = geoData->gFineNodesPerDir[2]*geoData->gNumFineNodes10;
    geoData->lNumFineNodes10 = geoData->lFineNodesPerDir[1]*geoData->lFineNodesPerDir[0];
    geoData->lNumFineNodes   = geoData->lFineNodesPerDir[2]*geoData->lNumFineNodes10;

    TEUCHOS_TEST_FOR_EXCEPTION(Coordinates->getLocalLength()
                               != static_cast<size_t>(geoData->lNumFineNodes),
                               Exceptions::RuntimeError,
                               "The local number of elements in Coordinates is not equal to the"
                               " number of nodes given by: lNodesPerDim!");
    TEUCHOS_TEST_FOR_EXCEPTION(Coordinates->getGlobalLength()
                               != static_cast<size_t>(geoData->gNumFineNodes),
                               Exceptions::RuntimeError,
                               "The global number of elements in Coordinates is not equal to the"
                               " number of nodes given by: gNodesPerDim!");

    // Get the coarsening rate
    std::string coarseningRate = pL.get<std::string>("aggregation: coarsening rate");
    Teuchos::Array<LO> coarseRate;
    try {
      coarseRate = Teuchos::fromStringToArray<LO>(coarseningRate);
    } catch(const Teuchos::InvalidArrayStringRepresentation e) {
      GetOStream(Errors,-1) << " *** \"aggregation: coarsening rate\" must be a string convertible into an array! *** "
                            << std::endl;
      throw e;
    }
    TEUCHOS_TEST_FOR_EXCEPTION((coarseRate.size() > 1) &&
                               (coarseRate.size() < geoData->numDimensions),
                               Exceptions::RuntimeError,
                               "\"aggregation: coarsening rate\" must have at least as many"
                               " components as the number of spatial dimensions in the problem.");

    for(int dim = 0; dim < 3; ++dim) {
      if(dim < geoData->numDimensions) {
        if(coarseRate.size() == 1) {
          geoData->coarseRate[dim] = coarseRate[0];
        } else if(coarseRate.size() == geoData->numDimensions) {
          geoData->coarseRate[dim] = coarseRate[dim];
        }
      } else {
        geoData->coarseRate[dim] = 1;
      }
    }

    // Build
    RCP<Aggregates> aggregates = rcp(new Aggregates(coordMap));
    aggregates->setObjectLabel("ST");
    aggregates->AggregatesCrossProcessors(true);

    // construct aggStat information
    std::vector<unsigned> aggStat(numLocalNodes, READY);


    // const RCP<const Teuchos::Comm<int> > comm = nodeMap->getComm();
    // GO numGlobalRows = 0;
    // if (IsPrint(Statistics1))
    //   MueLu_sumAll(comm, as<GO>(numLocalNodes), numGlobalRows);

    std::cout << "Use global lexicographic layout" << std::endl;

    LO numNonAggregatedNodes = numLocalNodes;
    GO numGlobalAggregatedPrev = 0, numGlobalAggsPrev = 0;
    if(geoData->meshLayout == "Global Lexicographic") {
      GlobalLexicographicLayout(coordMap, geoData, aggregates, aggStat, numNonAggregatedNodes);
    }

    TEUCHOS_TEST_FOR_EXCEPTION(numNonAggregatedNodes, Exceptions::RuntimeError,
                               "MueLu::StructuredAggregationFactory::Build: Leftover nodes found! Error!");

    std::cout << "p=" << coordMap->getComm()->getRank()
              << " | Recomputing the size of the aggregates." << std::endl;

    // aggregates->AggregatesCrossProcessors(false);
    aggregates->ComputeAggregateSizes(true/*forceRecompute*/);

    std::cout << "p=" << coordMap->getComm()->getRank()
              << " | Aggregate sizes recomputed!" << std::endl;

    Set(currentLevel, "Aggregates", aggregates);

    GetOStream(Statistics1) << aggregates->description() << std::endl;
  }

  template <class Scalar, class LocalOrdinal, class GlobalOrdinal, class Node>
  void StructuredAggregationFactory<Scalar, LocalOrdinal, GlobalOrdinal, Node>::
  GlobalLexicographicLayout(const RCP<const Map> coordMap, RCP<GeometricData> geoData,
                            RCP<Aggregates> aggregates, std::vector<unsigned>& aggStat,
                            LO& numNonAggregatedNodes) const {

    std::cout << "p=" << coordMap->getComm()->getRank()
              << " | gFineNodesPerDir: " << geoData->gFineNodesPerDir << std::endl;
    std::cout << "p=" << coordMap->getComm()->getRank()
              << " | coarseRate: " << geoData->coarseRate << std::endl;

    {
      GO tmp = 0;
      geoData->startIndices[2] = coordMap->getMinGlobalIndex()
        / (geoData->gFineNodesPerDir[1]*geoData->gFineNodesPerDir[0]);
      tmp = coordMap->getMinGlobalIndex()
        % (geoData->gFineNodesPerDir[1]*geoData->gFineNodesPerDir[0]);
      geoData->startIndices[1] = tmp / geoData->gFineNodesPerDir[0];
      geoData->startIndices[0] = tmp % geoData->gFineNodesPerDir[0];
    } // End of scope for tmp
    for(int dim = 0; dim < 3; ++dim) {
      geoData->startIndices[dim + 3] =
        geoData->startIndices[dim] + geoData->lFineNodesPerDir[dim] - 1;
    }

    for(int dim = 0; dim < 3; ++dim) {
      if(dim < geoData->numDimensions) {
        geoData->offsets[dim]     =
          Teuchos::as<LO>(geoData->startIndices[dim]) % geoData->coarseRate[dim];
        geoData->offsets[dim + 3] =
          Teuchos::as<LO>(geoData->startIndices[dim]) % geoData->coarseRate[dim];
      }
    }

    // Check if the partition contains nodes on a boundary, if so that boundary (face, line or
    // point) will not require ghost nodes, unless there is only one node in that direction.
    for(int dim = 0; dim < 3; ++dim) {
      if( (dim < geoData->numDimensions) &&
          (geoData->startIndices[dim] % geoData->coarseRate[dim] != 0 ||
           geoData->startIndices[dim] == geoData->gFineNodesPerDir[dim]-1)) {
        geoData->ghostInterface[2*dim] = true;
      }
      if( (dim < geoData->numDimensions) &&
          (geoData->startIndices[dim + 3] != geoData->gFineNodesPerDir[dim] - 1) &&
          (geoData->lFineNodesPerDir[dim] == 1 ||
           geoData->startIndices[dim + 3] % geoData->coarseRate[dim] != 0)) {
        geoData->ghostInterface[2*dim + 1] = true;
      }
    }

    std::cout << "p=" << coordMap->getComm()->getRank() << " | ";
    std::cout << "Geometric data:" << std::endl
              << "p=" << coordMap->getComm()->getRank() << " | "
              << "   coarseRate:         " << geoData->coarseRate << std::endl
              << "p=" << coordMap->getComm()->getRank() << " | "
              << "   gFineNodesPerDir:   " << geoData->gFineNodesPerDir << std::endl
              << "p=" << coordMap->getComm()->getRank() << " | "
              << "   lFineNodesPerDir:   " << geoData->lFineNodesPerDir << std::endl
              << "p=" << coordMap->getComm()->getRank() << " | "
              << "   startIndices:       " << geoData->startIndices << std::endl
              << "p=" << coordMap->getComm()->getRank() << " | "
              << "   offsets:            " << geoData->offsets << std::endl
              << "p=" << coordMap->getComm()->getRank() << " | "
              << "   ghostInterface:     {" << geoData->ghostInterface[0] << ", "
              << geoData->ghostInterface[1] << ", " << geoData->ghostInterface[2] << ", "
              << geoData->ghostInterface[3] << ", " << geoData->ghostInterface[4] << ", "
              << geoData->ghostInterface[5] << "}"<< std::endl;

    // Here one element can represent either the degenerate case of one node or the more general
    // case of two nodes, i.e. x---x is a 1D element with two nodes and x is a 1D element with one
    // node. This helps generating a 3D space from tensorial products...
    // A good way to handle this would be to generalize the algorithm to take into account the
    // discretization order used in each direction, at least in the FEM sense, since a 0 degree
    // discretization will have a unique node per element. This way 1D discretization can be viewed
    // as a 3D problem with one 0 degree element in the y direction and one 0 degre element in the z
    // direction.
    // !!! Operations below are aftecting both local and global values that have two different   !!!
    // orientations. Orientations can be interchanged using mapDirG2L and mapDirL2G. coarseRate,
    // endRate and offsets are in the global basis, as well as all the variables starting with a g,
    // !!! while the variables starting with an l are in the local basis.                        !!!
    for(int dim = 0; dim < 3; ++dim) {
      if(dim < geoData->numDimensions) {
        // This array is passed to the RAPFactory and eventually becomes gFineNodePerDir on the next
        // level.
        geoData->gCoarseNodesPerDir[dim] =
          (geoData->gFineNodesPerDir[dim] - 1) / geoData->coarseRate[dim];
        geoData->endRate[dim] =
          Teuchos::as<LO>((geoData->gFineNodesPerDir[dim] - 1) %geoData->coarseRate[dim]);
        if(geoData->endRate[dim] == 0) {
          geoData->endRate[dim] = geoData->coarseRate[dim];
          ++geoData->gCoarseNodesPerDir[dim];
        } else {
          geoData->gCoarseNodesPerDir[dim] += 2;
        }
      } else {
        geoData->endRate[dim] = 1;
        geoData->gCoarseNodesPerDir[dim] = 1;
      }
    }

    geoData->gNumCoarseNodes = geoData->gCoarseNodesPerDir[0]*geoData->gCoarseNodesPerDir[1]
      *geoData->gCoarseNodesPerDir[2];
    geoData->gNumCoarseNodes10 = geoData->gCoarseNodesPerDir[0]*geoData->gCoarseNodesPerDir[1];

    std::cout << "p=" << coordMap->getComm()->getRank() << " | ";
    std::cout << "   endRate:            " << geoData->endRate << std::endl
              << "p=" << coordMap->getComm()->getRank() << " | "
              << "   gCoarseNodesPerDir: " << geoData->gCoarseNodesPerDir << std::endl
              << "p=" << coordMap->getComm()->getRank() << " | "
              << "   gNumCoarseNodes:    " << geoData->gNumCoarseNodes << std::endl;

    for(LO dim = 0; dim < 3; ++dim) {
      if(dim < geoData->numDimensions) {
        // Check whether the partition includes the "end" of the mesh which means that endRate will
        // apply. Also make sure that endRate is not 0 which means that the mesh does not require a
        // particular treatment at the boundaries.
        if( (geoData->startIndices[dim] + geoData->lFineNodesPerDir[dim])
            == geoData->gFineNodesPerDir[dim] ) {
          geoData->lCoarseNodesPerDir[dim] = (geoData->lFineNodesPerDir[dim] - geoData->endRate[dim]
                                   + geoData->offsets[dim] - 1) / geoData->coarseRate[dim] + 1;
          if(geoData->offsets[dim] == 0) {++geoData->lCoarseNodesPerDir[dim];}
        } else {
          geoData->lCoarseNodesPerDir[dim] =
            (geoData->lFineNodesPerDir[dim] + geoData->offsets[dim] - 1) / geoData->coarseRate[dim];
          if(geoData->offsets[dim] == 0) {++geoData->lCoarseNodesPerDir[dim];}
        }
      } else {
        geoData->lCoarseNodesPerDir[dim] = 1;
      }
      // This would happen if the rank does not own any nodes but in that case a subcommunicator
      // should be used so this should really not be a concern.
      if(geoData->lFineNodesPerDir[dim] < 1) {geoData->lCoarseNodesPerDir[dim] = 0;}
    }

    geoData->lNumCoarseNodes = geoData->lCoarseNodesPerDir[0]*geoData->lCoarseNodesPerDir[1]
      *geoData->lCoarseNodesPerDir[2];
    geoData->lNumCoarseNodes10 = geoData->lCoarseNodesPerDir[0]*geoData->lCoarseNodesPerDir[1];

    std::cout << "p=" << coordMap->getComm()->getRank() << " | ";
    std::cout << "   lCoarseNodesPerDir: " << geoData->lCoarseNodesPerDir << std::endl
              << "p=" << coordMap->getComm()->getRank() << " | "
              << "   lNumCoarseNodes:    " << geoData->lNumCoarseNodes << std::endl;

    // For each direction, determine how many points (including ghosts) are required.
    bool ghostedDir[6] = {false};
    for(int dim = 0; dim < 3; ++dim) {
      // The first branch of this if-statement will be used if the rank contains only one layer
      // of nodes in direction i, that layer must also coincide with the boundary of the mesh
      // and coarseRate[i] == endRate[i]...
      if(dim < geoData->numDimensions) {
        if(geoData->startIndices[dim] == geoData->gFineNodesPerDir[dim] - 1 &&
           geoData->startIndices[dim] % geoData->coarseRate[dim] == 0) {
          geoData->startGhostedCoarseNode[dim] =
            geoData->startIndices[dim] / geoData->coarseRate[dim] - 1;
        } else {
          geoData->startGhostedCoarseNode[dim] =
            geoData->startIndices[dim] / geoData->coarseRate[dim];
        }
        geoData->ghostedCoarseNodesPerDir[dim] = geoData->lCoarseNodesPerDir[dim];
        // Check whether face *low needs ghost nodes
        if(geoData->ghostInterface[2*dim]) {
          geoData->ghostedCoarseNodesPerDir[dim] += 1;
          ghostedDir[2*dim] = true;
        }
        // Check whether face *hi needs ghost nodes
        if(geoData->ghostInterface[2*dim + 1]) {
          geoData->ghostedCoarseNodesPerDir[dim] += 1;
          ghostedDir[2*dim + 1] = true;
        }
      } else {
        geoData->startGhostedCoarseNode[dim] = 0;
        geoData->ghostedCoarseNodesPerDir[dim] = 1;
      }
    }

    LO numGhostedCoarseNodes = geoData->ghostedCoarseNodesPerDir[0]
      *geoData->ghostedCoarseNodesPerDir[1]*geoData->ghostedCoarseNodesPerDir[2];

    std::cout << "p=" << coordMap->getComm()->getRank() << " | ";
    std::cout << "   ghostedDir:         {" << ghostedDir[0] << ", " << ghostedDir[1] << ", "
              << ghostedDir[2] << ", " << ghostedDir[3] << ", "
              << ghostedDir[4] << ", " << ghostedDir[5] << "}" << std::endl
              << "p=" << coordMap->getComm()->getRank() << " | "
              << "   ghostedCoarseNodesPerDir: " << geoData->ghostedCoarseNodesPerDir << std::endl
              << "p=" << coordMap->getComm()->getRank() << " | "
              << "   numGhostedCoarseNodes: " << numGhostedCoarseNodes << std::endl
              << "p=" << coordMap->getComm()->getRank() << " | "
              << "   startGhostedCoarseNode:   " << geoData->startGhostedCoarseNode << std::endl;

    // aggregates->SetNumAggregates(geoData->lNumCoarseNodes);
    aggregates->SetNumAggregates(numGhostedCoarseNodes);

    // Find the GIDs, LIDs and PIDs of the coarse points on the fine mesh and coarse
    // mesh as this data will be used to fill vertex2AggId and procWinner vectors.
    LO lNumGhostedCoarseNodes = geoData->ghostedCoarseNodesPerDir[2]*
      geoData->ghostedCoarseNodesPerDir[1]*geoData->ghostedCoarseNodesPerDir[0];
    Array<GO> lCoarseNodeCoarseGIDs(geoData->lNumCoarseNodes),
      lCoarseNodeFineGIDs(geoData->lNumCoarseNodes);
    Array<GO> ghostedCoarseNodeCoarseGIDs(lNumGhostedCoarseNodes),
      ghostedCoarseNodeFineGIDs(lNumGhostedCoarseNodes);
    Array<LO> ghostedCoarseNodeCoarseIndices(3), ghostedCoarseNodeFineIndices(3), ijk(3);
    LO currentIndex = -1, countCoarseNodes = 0;
    for(ijk[2] = 0; ijk[2] < geoData->ghostedCoarseNodesPerDir[2]; ++ijk[2]) {
      for(ijk[1] = 0; ijk[1] < geoData->ghostedCoarseNodesPerDir[1]; ++ijk[1]) {
        for(ijk[0] = 0; ijk[0] < geoData->ghostedCoarseNodesPerDir[0]; ++ijk[0]) {
          currentIndex = (ijk[2]*geoData->ghostedCoarseNodesPerDir[1]
                          *geoData->ghostedCoarseNodesPerDir[0])
            + ijk[1]*geoData->ghostedCoarseNodesPerDir[0]
            + ijk[0];
          ghostedCoarseNodeCoarseIndices[0] = geoData->startGhostedCoarseNode[0] + ijk[0];
          ghostedCoarseNodeCoarseIndices[1] = geoData->startGhostedCoarseNode[1] + ijk[1];
          ghostedCoarseNodeCoarseIndices[2] = geoData->startGhostedCoarseNode[2] + ijk[2];
          GO myCoarseGID = ghostedCoarseNodeCoarseIndices[0]
            + ghostedCoarseNodeCoarseIndices[1]*geoData->gCoarseNodesPerDir[0]
            + (ghostedCoarseNodeCoarseIndices[2]*geoData->gCoarseNodesPerDir[1]
               *geoData->gCoarseNodesPerDir[0]);
          ghostedCoarseNodeCoarseGIDs[currentIndex] = myCoarseGID;
          GO myGID = 0, factor[3] = {};
          factor[2] = geoData->gNumFineNodes10;
          factor[1] = geoData->gFineNodesPerDir[0];
          factor[0] = 1;
          for(int dim = 0; dim < 3; ++dim) {
            if(dim < geoData->numDimensions) {
              if(geoData->startIndices[dim] - geoData->offsets[dim]
                 + ijk[dim]*geoData->coarseRate[dim] < geoData->gFineNodesPerDir[dim] - 1) {
                myGID += (geoData->startIndices[dim] - geoData->offsets[dim]
                          + ijk[dim]*geoData->coarseRate[dim])*factor[dim];
              } else {
                myGID += (geoData->startIndices[dim] - geoData->offsets[dim] + (ijk[dim] - 1)
                          *geoData->coarseRate[dim] + geoData->endRate[dim])*factor[dim];
              }
            }
          }
          if((!ghostedDir[0] || ijk[0] != 0)
             && (!ghostedDir[2] || ijk[1] != 0)
             && (!ghostedDir[4] || ijk[2] != 0)
             && (!ghostedDir[1] || ijk[0] != geoData->ghostedCoarseNodesPerDir[0] - 1)
             && (!ghostedDir[3] || ijk[1] != geoData->ghostedCoarseNodesPerDir[1] - 1)
             && (!ghostedDir[5] || ijk[2] != geoData->ghostedCoarseNodesPerDir[2] - 1)){
            lCoarseNodeCoarseGIDs[countCoarseNodes] = myCoarseGID;
            lCoarseNodeFineGIDs[countCoarseNodes]   = myGID;
            ++countCoarseNodes;
          }
          ghostedCoarseNodeFineGIDs[currentIndex] = myGID;
        }
      }
    }
    RCP<const Map> coarseCoordMap = MapFactory::Build (coordMap->lib(),
                                                       geoData->gNumCoarseNodes,
                                                       lCoarseNodeCoarseGIDs(),
                                                       coordMap->getIndexBase(),
                                                       coordMap->getComm());


    Array<int> ghostedCoarseNodeCoarsePIDs(lNumGhostedCoarseNodes);
    Array<LO>  ghostedCoarseNodeCoarseLIDs(lNumGhostedCoarseNodes);
    coarseCoordMap->getRemoteIndexList(ghostedCoarseNodeCoarseGIDs(),
                                       ghostedCoarseNodeCoarsePIDs(),
                                       ghostedCoarseNodeCoarseLIDs());

    // // Set all root nodes on the local processor
    // for(LO nodeIdx = 0; nodeIdx < geoData->lNumCoarseNodes; ++nodeIdx) {
    //       aggregates->SetIsRoot(lCoarseNodeFineLIDs[nodeIdx]);
    // }

    std::cout << "p=" << coordMap->getComm()->getRank() << " | "
              << "ghostedCoarseNodeCoarseGIDs: " << ghostedCoarseNodeCoarseGIDs << std::endl
              << "p=" << coordMap->getComm()->getRank() << " | "
              << "ghostedCoarseNodeCoarseLIDs: " << ghostedCoarseNodeCoarseLIDs << std::endl
              << "p=" << coordMap->getComm()->getRank() << " | "
              << "ghostedCoarseNodeFinePIDs:   " << ghostedCoarseNodeCoarsePIDs << std::endl
              << "p=" << coordMap->getComm()->getRank() << " | "
              << "lCoarseNodeCoarseGIDs:   " << lCoarseNodeCoarseGIDs << std::endl;

    // Now we are ready for the big loop over the fine node that will assign each
    // node on the fine grid to an aggregate and a processor.
    ArrayRCP<LO> vertex2AggId = aggregates->GetVertex2AggId()->getDataNonConst(0);
    ArrayRCP<LO> procWinner   = aggregates->GetProcWinner()  ->getDataNonConst(0);
    LO iGhosted, jGhosted, kGhosted, iCoarse, jCoarse, kCoarse, iRem, jRem, kRem;
    LO ghostedCoarseNodeCoarseLID, aggId;
    for(LO nodeIdx = 0; nodeIdx < geoData->lNumFineNodes; ++nodeIdx) {
      // Compute coarse ID associated with fine LID
      geoData->getFineNodeGhostedTuple(nodeIdx, iGhosted, jGhosted, kGhosted);
      iCoarse = iGhosted / geoData->coarseRate[0];
      iRem    = iGhosted % geoData->coarseRate[0];
      if(iRem > (geoData->coarseRate[0] / 2)) { ++iCoarse; }
      jCoarse = jGhosted / geoData->coarseRate[1];
      jRem    = jGhosted % geoData->coarseRate[1];
      if(jRem > (geoData->coarseRate[1] / 2)) { ++jCoarse; }
      kCoarse = kGhosted / geoData->coarseRate[2];
      kRem    = kGhosted % geoData->coarseRate[2];
      if(kRem > (geoData->coarseRate[2] / 2)) { ++kCoarse; }
      geoData->getCoarseNodeLID(iCoarse, jCoarse, kCoarse, ghostedCoarseNodeCoarseLID);

      aggId                 = ghostedCoarseNodeCoarseLIDs[ghostedCoarseNodeCoarseLID];
      vertex2AggId[nodeIdx] = aggId;
      procWinner[nodeIdx]   = ghostedCoarseNodeCoarsePIDs[ghostedCoarseNodeCoarseLID];
      aggStat[nodeIdx]      = AGGREGATED;
      --numNonAggregatedNodes;
    }

    std::cout << "p=" << coordMap->getComm()->getRank() << " | ";
    std::cout << "vertex2AggId: " << vertex2AggId() << std::endl
              << "p=" << coordMap->getComm()->getRank() << " | "
              << "procWinner:   " << procWinner() << std::endl;

  } // GlobalLexicographicLayout

} //namespace MueLu


#endif /* MUELU_STRUCTUREDAGGREGATIONFACTORY_DEF_HPP_ */
