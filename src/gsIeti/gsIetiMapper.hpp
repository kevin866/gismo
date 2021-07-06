/** @file gsIetiMapper.hpp

    @brief Algorithms that help with assembling the matrices required for IETI-solvers

    This file is part of the G+Smo library.

    This Source Code Form is subject to the terms of the Mozilla Public
    License, v. 2.0. If a copy of the MPL was not distributed with this
    file, You can obtain one at http://mozilla.org/MPL/2.0/.

    Author(s): S. Takacs
*/

#pragma once

#include <gsAssembler/gsGenericAssembler.h>

/*    Concerning the status flag m_status:
 *       (m_status&1)!=0    means that the object has been initialized by calling init or the value constructor
 *       (m_status&2)!=0    means that there are artificial dofs
 *       (m_status&4)!=0    means that the jump matrices have been computed
 *       (m_status&8)!=0    means that corners have been set up as primal constraints
 *       (m_status&flag)!=0 for flag = 16, 32,... means that edges, faces, ... have been set up as primal constraints
 *
 *   This class allows that the dof mappers have more dofs than the bases.
 *   It is assumed the first N0 basis functions in the mapper are associated
 *   to the basis. The remaining N-N0 ones are artificial ones. We can only
 *   know where they belong, if we go through the global mapper
 *     N  = m_dofMapperGlobal.patchSize(k);
 *     N0 = m_multiBasis->piece(k).size();
 *
 *   If for one match N!=N0, we set status flag 2.
 */
// TODO: go through the whole class and check for this

namespace gismo
{

#define DEBUGVAR(v) gsInfo << #v << ": " << (v) << "\n"
#define DEBUGMAT(m) gsInfo << #m << ": " << (m).rows() << "x" << (m).cols() << "\n"


template <class T>
void gsIetiMapper<T>::init(
        const gsMultiBasis<T>& multiBasis,
        gsDofMapper dofMapperGlobal,
        const Matrix& fixedPart
    )
{
    GISMO_ASSERT( dofMapperGlobal.componentsSize() == 1, "gsIetiMapper::init: "
        "Got only 1 multi basis, so a gsDofMapper with only 1 component is expected." );
    GISMO_ASSERT( dofMapperGlobal.numPatches() == multiBasis.nBases(), "gsIetiMapper::init: "
        "Number of patches does not agree." );

    const index_t nPatches = dofMapperGlobal.numPatches();
    m_multiBasis = &multiBasis;
    m_dofMapperGlobal = give(dofMapperGlobal);
    m_dofMapperLocal.clear();
    m_dofMapperLocal.resize(nPatches);
    m_fixedPart.clear();
    m_fixedPart.resize(nPatches);
    m_jumpMatrices.clear();
    m_nPrimalDofs = 0;
    m_primalConstraints.clear();
    m_primalConstraints.resize(nPatches);
    m_primalDofIndices.clear();
    m_primalDofIndices.resize(nPatches);
    m_status = 1;

    for (index_t k=0; k<nPatches; ++k)
    {
        const index_t nDofs = m_dofMapperGlobal.patchSize(k);
        GISMO_ASSERT( nDofs>=m_multiBasis->piece(k).size(), "gsIetiMapper::init: "
            "The mapper for patch "<<k<<" has not as many dofs as the corresponding basis." );

        if (nDofs>m_multiBasis->piece(k).size())
            m_status |= 2;

        m_dofMapperLocal[k].setIdentity(1,nDofs);

        // Eliminate boundary dofs (we do not consider the full floating case).
        for (index_t i=0; i<nDofs; ++i)
        {
            const index_t idx = m_dofMapperGlobal.index(i,k);
            if (m_dofMapperGlobal.is_boundary_index(idx))
                m_dofMapperLocal[k].eliminateDof(i,0);
        }
        m_dofMapperLocal[k].finalize();

        const index_t szFixedPart = m_dofMapperLocal[k].boundarySize();
        m_fixedPart[k].setZero(szFixedPart,1);
        for (index_t i=0; i<nDofs; ++i)
        {
            const index_t idx = m_dofMapperGlobal.index(i,k);
            if (m_dofMapperGlobal.is_boundary_index(idx))
            {
                const index_t globalBoundaryIdx = m_dofMapperGlobal.bindex(i,k);
                const index_t localBoundaryIdx = m_dofMapperLocal[k].bindex(i,0);
                m_fixedPart[k](localBoundaryIdx,0) = fixedPart(globalBoundaryIdx,0);
            }
        }
    }

    if (m_status&2)
    {
        // Populate m_artificialDofInfo
        const index_t nDofs = m_dofMapperGlobal.freeSize();
        gsMatrix<index_t> dofs(nDofs,2);   // Has information (patch, localIndex) for each global dof
        dofs.setZero();

        for (index_t k=0; k<nPatches; ++k)
        {
            // Here we only consider the real values
            const index_t sz = m_multiBasis->piece(k).size();
            for (index_t i=0; i<sz; ++i)
            {
                const index_t globalIndex = m_dofMapperGlobal.index(i,k);
                if (m_dofMapperGlobal.is_free_index(globalIndex))
                {
                    GISMO_ASSERT( dofs(globalIndex,0) == 0, "Internal error.");
                    dofs(globalIndex,0) = k;
                    dofs(globalIndex,1) = m_dofMapperLocal[k].index(i,0) + 1;
                }
            }
        }

        m_artificialDofInfo.resize( nPatches );
        for (index_t k=0; k<nPatches; ++k)
        {
            const index_t sz  = m_multiBasis->piece(k).size();
            const index_t sz2 = m_dofMapperGlobal.patchSize(k);
            for (index_t i=sz; i<sz2; ++i)
            {
                const index_t globalIndex       = m_dofMapperGlobal.index(i,k);
                if (m_dofMapperGlobal.is_free_index(globalIndex))
                {
                    const index_t otherPatch        = dofs(globalIndex,0);
                    const index_t indexOnOtherPatch = dofs(globalIndex,1) - 1;
                    GISMO_ASSERT( indexOnOtherPatch>=0, "Internal error." );
                    gsVector<index_t> & which = m_artificialDofInfo[otherPatch][k];
                    if (which.rows() == 0)
                        which.setZero( m_dofMapperLocal[otherPatch].freeSize(), 1 );
                    which[indexOnOtherPatch] = i + 1;
                }
            }
        }
    }
}


template <class T>
typename gsIetiMapper<T>::Matrix
gsIetiMapper<T>::constructGlobalSolutionFromLocalSolutions( const std::vector<Matrix>& localContribs )
{
    GISMO_ASSERT( m_status&1, "gsIetiMapper: The class has not been initialized." );

    const index_t nPatches = m_dofMapperGlobal.numPatches();
    GISMO_ASSERT( nPatches == static_cast<index_t>(localContribs.size()),
        "gsIetiMapper::constructGlobalSolutionFromLocalSolutions; The number of local contributions does "
        "not argee with the number of patches." );

    Matrix result;
    result.setZero( m_dofMapperGlobal.freeSize(), localContribs[0].cols() );

    // We are never extracting the solution from artificial dofs
    for (index_t k=0; k<nPatches; ++k)
    {
        const index_t sz=m_multiBasis->piece(k).size();
        for (index_t i=0; i<sz; ++i)
        {
            // There is an asignment. This means that if there are several values, we just take the last one.
            if (m_dofMapperLocal[k].is_free(i,0) && m_dofMapperGlobal.is_free(i,k))
                result.row(m_dofMapperGlobal.index(i,k)) = localContribs[k].row(m_dofMapperLocal[k].index(i,0));
        }
    }
    return result;
}

namespace {
struct dof_helper {
    index_t globalIndex;
    index_t patch;
    index_t localIndex;
    bool operator<(const dof_helper& other) const
    {
        return globalIndex < other.globalIndex;
    }
};
}

template <class T>
void gsIetiMapper<T>::cornersAsPrimals()
{
    GISMO_ASSERT( m_status&1, "gsIetiMapper: The class has not been initialized." );
    GISMO_ASSERT( !(m_status&8), "gsIetiMapper::cornersAsPrimals: This function has already been called." );
    m_status |= 8;

    const index_t nPatches = m_dofMapperLocal.size();

    // Construct all corners
    std::vector<dof_helper> corners;
    const index_t dim = m_multiBasis->dim();
    corners.reserve((1<<dim)*nPatches);
    // Add corners on all patches
    for (index_t k=0; k<nPatches; ++k)
    {
        for (boxCorner it = boxCorner::getFirst(dim); it!=boxCorner::getEnd(dim); ++it)
        {
            const index_t idx = (*m_multiBasis)[k].functionAtCorner(it);
            dof_helper dh;
            dh.globalIndex = m_dofMapperGlobal.index( idx, k );
            dh.patch = k;
            dh.localIndex = m_dofMapperLocal[k].index( idx, 0 );
            if (m_dofMapperGlobal.is_free_index(dh.globalIndex))
            {
                if (m_status&2)
                {
                    // If there artificial dofs, we have to find all pre-images, which are then mapped back
                    std::vector< std::pair<index_t,index_t> > preImages;
                    m_dofMapperGlobal.preImage(dh.globalIndex, preImages);
                    gsInfo << "Found " << preImages.size() << " pre-images.\n";
                    for (size_t i=0; i<preImages.size(); ++i)
                    {
                        dof_helper dh2;
                        dh2.globalIndex = dh.globalIndex;
                        dh2.patch = preImages[i].first;
                        dh2.localIndex = m_dofMapperLocal[preImages[i].first].index( preImages[i].second, 0 );
                        corners.push_back(give(dh2));
                    }
                }
                else
                {
                    // Store the corner
                    corners.push_back(give(dh));
                }
            }
        }
    }

    // Sort corners to collapse corners with same global index
    std::sort(corners.begin(), corners.end());

    // Create data
    index_t lastIndex = -1;
    const index_t sz = corners.size();
    for (index_t i=0; i<sz; ++i)
    {
        if (lastIndex!=corners[i].globalIndex)
        {
            lastIndex = corners[i].globalIndex;
            ++m_nPrimalDofs;
        }
        const index_t cornerIndex = m_nPrimalDofs - 1;
        const index_t patch       = corners[i].patch;
        const index_t localIndex  = corners[i].localIndex;
        
        SparseVector constr(m_dofMapperLocal[patch].freeSize());
        constr[localIndex] = 1;

        m_primalConstraints[patch].push_back(give(constr));
        m_primalDofIndices[patch].push_back(cornerIndex);
    }

}

template <class T>
gsSparseVector<T> gsIetiMapper<T>::assembleAverage(
    const gsGeometry<T>& geo,
    const gsBasis<T>& basis,
    const gsDofMapper& dm,
    boxComponent bc
)
{
    gsMatrix<index_t> indices;

    gsMatrix<T> moments = gsGenericAssembler<T>(
        *(geo.component(bc)),
        *(basis.componentBasis_withIndices(bc, indices, false))
    ).assembleMoments(
        gsConstantFunction<T>(1,geo.targetDim())
    );

    SparseVector constraint( dm.freeSize() );
    T sum = (T)0;
    const index_t sz = moments.size();
    GISMO_ASSERT( sz == indices.size(), "Internal error." );
    for (index_t i=0; i<sz; ++i)
    {
        const index_t idx = dm.index( indices(i,0), 0 );
        if (dm.is_free_index(idx))
        {
            constraint[idx] = moments(i,0);
            sum += moments(i,0);
        }
    }
    return constraint / sum;

}

namespace {
template <class T>
struct constr_helper {
    std::vector<index_t> globalIndices;
    gsSparseVector<T> vector;
    index_t patch;
    bool operator<(const constr_helper& other) const
    {
        const size_t sz = globalIndices.size();
        if (sz != other.globalIndices.size())
        {
            //gsInfo << "sz" << "<" << other.globalIndices.size() << "\n";
            return sz < other.globalIndices.size();
        }
        for (size_t i=0; i<sz; ++i)
            if (globalIndices[i] != other.globalIndices[i])
            {
                //gsInfo << "Diff in " << i << ": " << globalIndices[i] << "<" << other.globalIndices[i] << "\n";
                return globalIndices[i] < other.globalIndices[i];
            }
        //gsInfo << "No diff\n";
        return false;
    }
};
}

template <class T>
void gsIetiMapper<T>::interfaceAveragesAsPrimals( const gsMultiPatch<T>& geo, const short_t d )
{
    GISMO_ASSERT( m_status&1, "gsIetiMapper: The class has not been initialized." );
    GISMO_ASSERT( d>0, "gsIetiMapper::interfaceAveragesAsPrimals cannot handle corners." );
    GISMO_ASSERT( d<=m_multiBasis->dim(), "gsIetiMapper::interfaceAveragesAsPrimals: "
        "Interfaces cannot have larger dimension than considered object." );
    GISMO_ASSERT( (index_t)(geo.nPatches()) == m_multiBasis->nPieces(),
        "gsIetiMapper::interfaceAveragesAsPrimals: The given geometry does not fit.");
    GISMO_ASSERT( geo.parDim() == m_multiBasis->dim(),
        "gsIetiMapper::interfaceAveragesAsPrimals: The given geometry does not fit.");

    const unsigned flag = 1<<(3+d);
    GISMO_ASSERT( !(m_status&flag), "gsIetiMapper::interfaceAveragesAsPrimals: This function has "
        "already been called for d="<<d );
    m_status |= flag;

    std::vector< std::vector<patchComponent> > components = geo.allComponents();
    const index_t nComponents = components.size();
    for (index_t n=0; n<nComponents; ++n)
    {
        const index_t sz = components[n].size();
        if ( components[n][0].dim() == d )
        {
            std::vector< constr_helper<T> > constraints;
            constraints.reserve(sz);

            for (index_t i=0; i<sz; ++i)
            {
                constr_helper<T> constraint;
                constraint.patch = components[n][i].patch();
                constraint.vector = assembleAverage(
                    geo[constraint.patch],
                    (*m_multiBasis)[constraint.patch],
                    m_dofMapperLocal[constraint.patch],
                    components[n][i]
                );
                if ( constraint.vector.nonZeros() > 0 )
                {
                    std::map<index_t,index_t> inverse = m_dofMapperLocal[constraint.patch].inverseOnPatch(0);
                    // Store and sort global indices for constraint
                    constraint.globalIndices.reserve( constraint.vector.nonZeros() );
                    for (typename SparseVector::InnerIterator it(constraint.vector); it; ++it)
                    {
                        const index_t idx = m_dofMapperGlobal.index( inverse[it.row()], constraint.patch );
                        constraint.globalIndices.push_back(idx);
                    }
                    std::sort(constraint.globalIndices.begin(), constraint.globalIndices.end());

                    // Transfer constraint to artificial ifaces
                    if (m_status&2)
                    {
                        for (std::map< index_t, gsVector<index_t> >::const_iterator it = m_artificialDofInfo[constraint.patch].begin();
                             it != m_artificialDofInfo[constraint.patch].end(); ++it)
                        {
                            bool ok = true;
                            for (typename SparseVector::InnerIterator v_it(constraint.vector); v_it; ++v_it)
                                if (it->second[v_it.row()]==0)
                                    ok = false;

                            if (ok)
                            {
                                constr_helper<T> constraint2;
                                constraint2.patch = it->first;

                                constraint2.vector.resize( m_dofMapperLocal[constraint2.patch].freeSize(), 1 );
                                for (typename SparseVector::InnerIterator v_it(constraint.vector); v_it; ++v_it)
                                {
                                    const index_t idx = m_dofMapperLocal[constraint2.patch].index( it->second[v_it.row()] - 1, 0 );
                                    constraint2.vector[idx] = v_it.value();
                                }

                                constraint2.globalIndices = constraint.globalIndices;
                                constraints.push_back(give(constraint2));
                            }
                        }
                    }

                    constraints.push_back(give(constraint));

                }
            }

            // Sort constraints to collapse constraints with same global indices
            std::sort(constraints.begin(), constraints.end());

            // Construct data
            const index_t nConstraints = constraints.size();
            for (index_t i=0; i<nConstraints; ++i)
            {
                bool ignore = false;
                if ( i==0 || constraints[i-1]<constraints[i] )
                {
                    if ( (i<nConstraints-1 && ! (constraints[i]<constraints[i+1])) || m_multiBasis->dim() == d )
                        ++m_nPrimalDofs;
                    else
                        ignore = true; // Ignore constraints that are not shared between patches (except if it is the average in the interior)
                }
                DEBUGVAR(nConstraints);
                DEBUGVAR(i);
                DEBUGVAR(m_nPrimalDofs);
                DEBUGVAR(ignore);
                DEBUGVAR(constraints[i].vector.transpose());
                if (!ignore)
                {
                    const index_t & k = constraints[i].patch;
                    m_primalConstraints[k].push_back(give(constraints[i].vector));
                    m_primalDofIndices[k].push_back(m_nPrimalDofs-1);
                }
            }

            //TODO: GISMO_ASSERT( used==0 || used == sz, "Internal error: sz="<<sz<<", used="<<used );

        }
    }
}


template <class T>
void gsIetiMapper<T>::customPrimalConstraints( std::vector< std::pair<index_t,SparseVector> > data )
{
    GISMO_ASSERT( m_status&1, "gsIetiMapper: The class has not been initialized." );

    const index_t sz = data.size();
    for (index_t i=0; i<sz; ++i)
    {
        const index_t patch = data[i].first;
        m_primalConstraints[patch].push_back(give(data[i].second));
        m_primalDofIndices[patch].push_back(m_nPrimalDofs);
    }
    ++m_nPrimalDofs;
}

template <class T>
std::vector<index_t> gsIetiMapper<T>::skeletonDofs( const index_t patch ) const
{
    GISMO_ASSERT( m_status&1, "gsIetiMapper: The class has not been initialized." );

    std::vector<index_t> result;
    const index_t patchSize = m_dofMapperGlobal.patchSize(patch);
    const index_t dim = m_multiBasis->dim();
    result.reserve(2*dim*std::pow(patchSize,(1.0-dim)/dim));
    for (index_t i=0; i<patchSize; ++i)
        if ( m_dofMapperGlobal.is_coupled(i,patch) )
            result.push_back(m_dofMapperLocal[patch].index(i,0));
    return result;
}

template <class T>
void gsIetiMapper<T>::computeJumpMatrices( bool fullyRedundant, bool excludeCorners )
{
    GISMO_ASSERT( m_status&1, "gsIetiMapper: The class has not been initialized." );
    GISMO_ASSERT( !(m_status&4), "gsIetiMapper::computeJumpMatrices: This function has already been called." );
    m_status |= 4;

    const index_t nPatches = m_dofMapperGlobal.numPatches();
    const index_t coupledSize = m_dofMapperGlobal.coupledSize();

    std::vector< std::vector< std::pair<index_t,index_t> > > coupling;
    coupling.resize(coupledSize);

    // Find the groups of to be coupled indices in the global mapper
    for (index_t k=0; k<nPatches; ++k)
    {
        const index_t patchSize = m_dofMapperGlobal.patchSize(k);
        for (index_t i=0; i<patchSize; ++i)
        {
            const index_t globalIndex = m_dofMapperGlobal.index(i,k);
            if ( m_dofMapperGlobal.is_coupled_index(globalIndex) )
            {
                const index_t coupledIndex = m_dofMapperGlobal.cindex(i,k);
                const index_t localIndex = m_dofMapperLocal[k].index(i,0);
                coupling[coupledIndex].push_back(
                    std::pair<index_t,index_t>(k,localIndex)
                );
            }
        }
    }

    // Erease data for corners if so desired
    if (excludeCorners)
    {
        const index_t dim = m_multiBasis->dim();
        for (index_t k=0; k<nPatches; ++k)
        {
            for (boxCorner it = boxCorner::getFirst(dim); it!=boxCorner::getEnd(dim); ++it)
            {
                const index_t idx = (*m_multiBasis)[k].functionAtCorner(it);
                const index_t globalIndex = m_dofMapperGlobal.index(idx,k);
                if ( m_dofMapperGlobal.is_coupled_index(globalIndex) )
                {
                    const index_t coupledIndex = m_dofMapperGlobal.cindex(idx,k);
                    coupling[coupledIndex].clear();
                }
            }
        }
    }

    // Compute the number of Lagrange multipliers
    index_t numLagrangeMult = 0;
    for (index_t i=0; i<coupledSize; ++i)
    {
        const index_t n = coupling[i].size();
        GISMO_ASSERT( n>1 || excludeCorners, "gsIetiMapper::computeJumpMatrices:"
            "Found a coupled dof that is not coupled to any other dof." );
        if (fullyRedundant)
            numLagrangeMult += (n * (n-1))/2;
        else
            numLagrangeMult += n-1;
    }

    // Compute the jump matrices
    std::vector< gsSparseEntries<T> > jumpMatrices_se(nPatches);
    for (index_t i=0; i<nPatches; ++i)
    {
        const index_t dim = m_multiBasis->dim();
        jumpMatrices_se[i].reserve(std::pow(m_dofMapperLocal[i].freeSize(),(1.0-dim)/dim));
    }

    index_t multiplier = 0;
    for (index_t i=0; i<coupledSize; ++i)
    {
        const index_t n = coupling[i].size();
        const index_t maxIndex = fullyRedundant ? (n-1) : 1;
        for (index_t j1=0; j1<maxIndex; ++j1)
        {
            for (index_t j2=j1+1; j2<n; ++j2)
            {
                const index_t patch1 = coupling[i][j1].first;
                const index_t localIndex1 = coupling[i][j1].second;
                const index_t patch2 = coupling[i][j2].first;
                const index_t localIndex2 = coupling[i][j2].second;
                jumpMatrices_se[patch1].add(multiplier,localIndex1,(T)1);
                jumpMatrices_se[patch2].add(multiplier,localIndex2,(T)-1);
                ++multiplier;
            }
        }
    }
    GISMO_ASSERT( multiplier == numLagrangeMult, "gsIetiMapper::computeJumpMatrices: Internal error: "
        << multiplier << "!=" << numLagrangeMult );

    m_jumpMatrices.clear();
    for (index_t i=0; i<nPatches; ++i)
    {
        m_jumpMatrices.push_back(JumpMatrix(numLagrangeMult, m_dofMapperLocal[i].freeSize()));
        m_jumpMatrices[i].setFrom(jumpMatrices_se[i]);
    }

}


} // namespace gismo
