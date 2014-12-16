/** @file gsAssemblerBase.h

    @brief Provides generic assembler routines

    This file is part of the G+Smo library.

    This Source Code Form is subject to the terms of the Mozilla Public
    License, v. 2.0. If a copy of the MPL was not distributed with this
    file, You can obtain one at http://mozilla.org/MPL/2.0/.

    Author(s): A. Mantzaflaris
*/

#pragma once

#include <gsCore/gsForwardDeclarations.h>

#include <gsCore/gsBasisRefs.h>
#include <gsCore/gsDofMapper.h>
#include <gsCore/gsStdVectorRef.h>
#include <gsCore/gsAffineFunction.h> // needed by DG

namespace gismo
{

/** @brief The assembler class provides to generic routines for volume
 * and boundary integrals that are used for for matrix and rhs
 * generation
*/
template <class T>
class gsAssemblerBase
{
private:
    typedef gsStdVectorRef<gsDofMapper> gsDofMappers;

public:

    /// @brief Constructor using a multipatch domain
    /// \note Rest of the data fields should be initialized in a
    /// derived constructor
    gsAssemblerBase(const gsMultiPatch<T> & patches) :
    m_patches(patches)
    { }

    ~gsAssemblerBase()
    { }

    /// @brief Generic assembly routine for volume or boundary integrals
    template<class ElementVisitor>
    void apply(ElementVisitor & visitor, 
               int patchIndex = 0, 
               boxSide side = boundary::none)
    {
        //gsDebug<< "Apply to patch "<< patchIndex <<"("<< side <<")\n";

        const gsBasisRefs<T> bases(m_bases, patchIndex);
        const gsDofMappers mappers(m_dofMappers);
        
        gsMatrix<T> quNodes  ; // Temp variable for mapped nodes
        gsVector<T> quWeights; // Temp variable for mapped weights
        gsQuadRule<T> QuRule; // Reference Quadrature rule
        unsigned evFlags(0);

        // Initialize 
        visitor.initialize(bases, QuRule, evFlags);

        // Initialize geometry evaluator -- TODO: Initialize inside visitor
        typename gsGeometry<T>::Evaluator geoEval( 
            m_patches[patchIndex].evaluator(evFlags));

        // Initialize domain element iterator -- using unknown 0
        typename gsBasis<T>::domainIter domIt = bases[0].makeDomainIterator(side);

        // Start iteration over elements
        for (; domIt->good(); domIt->next() )
        {
            // Map the Quadrature rule to the element
            QuRule.mapTo( domIt->lowerCorner(), domIt->upperCorner(), quNodes, quWeights );

            // Perform required evaluations on the quadrature nodes
            visitor.evaluate(bases, /* *domIt,*/ *geoEval, quNodes);
            
            // Assemble on element
            visitor.assemble(*domIt, *geoEval, quWeights);
            
            // Push to global matrix and right-hand side vector
            visitor.localToGlobal(mappers, m_ddof, patchIndex, m_matrix, m_rhs);
        }
    }


    /// @brief Generic assembly routine for patch-interface integrals
    template<class InterfaceVisitor>
    void apply(InterfaceVisitor & visitor, 
               const boundaryInterface & bi)
    {
        //gsDebug<<"Apply DG on "<< bi <<".\n";

        const gsDofMappers mappers(m_dofMappers);
        const gsAffineFunction<T> interfaceMap(m_patches.getMapForInterface(bi));

        const int patch1      = bi[0].patch;
        const int patch2      = bi[1].patch;
        const gsBasis<T> & B1 = m_bases[0][patch1];// (!) unknown 0
        const gsBasis<T> & B2 = m_bases[0][patch2];

        const int bSize1      = B1.numElements();
        const int bSize2      = B2.numElements();
        const int ratio = bSize1 / bSize2;
        GISMO_ASSERT(bSize1 >= bSize2 && bSize1%bSize2==0,
                     "DG assumes nested interfaces.");
        
        gsMatrix<T> quNodes1, quNodes2;// Mapped nodes
        gsVector<T> quWeights;         // Mapped weights
        // Evaluation flags for the Geometry map
        unsigned evFlags(0);
        
        // Initialize 
        visitor.initialize(B1, B2, QuRule, evFlags);
        
        // Initialize geometry evaluators
        typename gsGeometry<T>::Evaluator geoEval1( 
            m_patches[patch1].evaluator(evFlags));
        typename gsGeometry<T>::Evaluator geoEval2( 
            m_patches[patch2].evaluator(evFlags));

        // Initialize domain element iterators
        typename gsBasis<T>::domainIter domIt1 = B1.makeDomainIterator( bi.first() .side() );
        typename gsBasis<T>::domainIter domIt2 = B2.makeDomainIterator( bi.second().side() );
        
        int count = 0;
        // iterate over all boundary grid cells on the "left"
        for (; domIt1->good(); domIt1->next() )
        {
            count++;
            // Get the element of the other side in domIter2
            //domIter1->adjacent( bi.orient, *domIter2 );
            
            // Compute the quadrature rule on both sides
            QuRule.mapTo( domIt1->lowerCorner(), domIt1->upperCorner(), quNodes1, quWeights);
            interfaceMap.eval_into(quNodes1,quNodes2);

            // Perform required evaluations on the quadrature nodes            
            visitor.evaluate(B1, *geoEval1, B2, *geoEval2, quNodes1, quNodes2);

            // Assemble on element
            visitor.assemble(*domIt1, *geoEval1, *geoEval2, quWeights);
            
            // Push to global patch matrix (m_rhs is filled in place)
            visitor.localToGlobal(mappers, patch1, patch2, m_matrix, m_rhs);
            
            if ( count % ratio == 0 ) // next master element ?
                domIt2->next();
        }
    }


public:

    /// @brief Return the multipatch.
    const gsMultiPatch<T> & patches() const { return m_patches; }

    /// @brief Return the multi-basis
    const gsMultiBasis<T> & multiBasis(index_t k) const { return m_bases[k]; }

    /// @brief Return the DOF mapper for unknown \em i.
    const gsDofMapper& dofMapper(unsigned i = 0) const     { return m_dofMappers[i]; }

    /// @brief Returns the left-hand global matrix
    const gsSparseMatrix<T> & matrix() const { return m_matrix; }

    /// @brief Returns the left-hand side vector(s)
    /// ( multiple right hand sides possible )
    const gsMatrix<T> & rhs() const { return m_rhs; }

    /// @brief Returns the number of (free) degrees of freedom
    int numDofs() const { return m_dofs; }
    
protected:

    /// The multipatch domain
    gsMultiPatch<T> m_patches;

    /// The discretization bases corresponding to \a m_patches and to
    /// the number of solution fields that are to be computed
    /// m_bases[i]: The multi-basis for unknown i
    std::vector< gsMultiBasis<T> > m_bases;
    
    /// The Dof mapper is used to map patch-local DoFs to the global DoFs
    /// One for each unknown, one for each patch
    /// m_dofMappers[i]: DoF Mapper for unknown i
    std::vector<gsDofMapper>  m_dofMappers;
    
    // Dirichlet DoF fixed values (if applicable)
    gsMatrix<T> m_ddof;

    // Reference Quadrature rule
    gsQuadRule<T> QuRule;

    // *** Outputs *** 
    
    /// Global matrix
    gsSparseMatrix<T> m_matrix;

    /// Right-hand side ( multiple right hand sides possible )
    gsMatrix<T>       m_rhs;

    // *** Information *** 

    /// number of degrees of freedom (excluding eliminated etc)
    // to do: rename to m_matSize
    int m_dofs;

};


} // namespace gismo

