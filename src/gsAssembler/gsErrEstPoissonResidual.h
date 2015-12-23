/** @file gsErrEstPoissonResidual.h

    @brief Residual-type error estimator for the Poisson problem.

    This file is part of the G+Smo library.

    This Source Code Form is subject to the terms of the Mozilla Public
    License, v. 2.0. If a copy of the MPL was not distributed with this
    file, You can obtain one at http://mozilla.org/MPL/2.0/.

    Author(s): S. Kleiss
*/

#pragma once

#include <gismo.h>

namespace gismo
{


/** \brief Provides a residual-type and element-wise error estimator
 * for the Poisson problem.
 *
 * Let the Poisson problem on the domain \f$ \Omega \f$ be given by
 * \f[
  -\Delta u = f,\quad u = g_D \mathrm{\ on\ } \Gamma_D,\quad u = g_N \mathrm{\ on\ } \Gamma_N,
\f]
 * where\n
 * \f$ f \f$ is a given right-hand-side,\n
 * \f$ g_D \f$ is given Dirichlet data, \f$ \Gamma_D\f$ is the Dirichlet boundary,\n
 * \f$ g_N \f$ is given Neumann data, \f$ \Gamma_N\f$ is the Neumann boundary.
 *
 * The error estimate \f$\eta\f$ for a computed discrete solution
 * \f$ u_h \f$ is given by
 * \f[ \eta^2 = \sum_K \eta_K^2 \f]
 * where the local estimate \f$ \eta_K \f$ on an element \f$ K \f$ is given by
     \f[
     \eta_K^2 =
     h^2 \int_K ( \Delta u_h + f )^2 dx
     + h \int_{\partial K \cap \Gamma_N} ( g_N - \partial_n u_h )^2 ds
     + h \int_{\partial K \cap \partial \Omega'} ( \partial_n u_h - \partial_n u_h' )^2 ds
     \f]
 * \f$ h \f$ denotes the size of element \f$ K \f$,\n
 * \f$ \partial_n u  = \nabla u \cdot \vec{n} \f$ denotes the normal derivative (\f$ \vec{n} \f$ is the outer unit-normal vector to the current patch), and\n
 * \f$ u_h' \f$ denotes the discrete solution on the neighbouring
 * patch \f$ \Omega' \f$.
 *
 * \ingroup Assembler
 */
template <class T>
class gsErrEstPoissonResidual : public gsNorm<T>
{
    friend class gsNorm<T>;

public:

    // f1 in gsNorm corresponds to discrete Solution
    // f2 in gsNorm corresponds to right-hand-side


    /**
     * \brief Constructor
     * \param _discSolution Discrete solution
     * \param _rhsFunction Right-hand-side-/Source-function \f$ f \f$
     * of the Poisson problem.
     * \param bcInfo Boundary conditions
     * \param _rhsFunctionParam Flag indicating whether the \em _rhsFunction
     * is parameterized (if true, the evaluation points must be given
     * on the parameter domain)
     *
     */
    gsErrEstPoissonResidual(const gsField<T> & _discSolution,
                            const gsFunction<T> & _rhsFunction,
                            const gsBoundaryConditions<T> & bcInfo,
                            bool _rhsFunctionParam = false)
    : gsNorm<T>(_discSolution,_rhsFunction), m_bcInfo(bcInfo), m_f2param(_rhsFunctionParam)
    {    }



     /** \brief Constructor
     * \param _discSolution Discrete solution
     * \param _rhsFunction Right-hand-side-/Source-function \f$ f\f$
     * of the Poisson problem.
     * \param _rhsFunctionParam Flag indicating whether the \em _rhsFunction
     * is parameterized (in this case, the evaluation points must be given
     * on the parameter domain
     */
    gsErrEstPoissonResidual(const gsField<T> & _discSolution,
             const gsFunction<T> & _rhsFunction,
             bool _rhsFunctionParam = false)
    : gsNorm<T>(_discSolution,_rhsFunction), m_f2param(_rhsFunctionParam)
    {
        /*! this is buggy..
        // In case no boundary conditions are provided, we still
        // need to initialize m_bcInfo.
        // Unpretty workaround, setting up m_bcInfo with only
        // one homogenous Neumann boundary condition.
        gsBoundaryConditions<T> bcInfo;
        gsFunctionExpr<T>  g("0", _rhsFunction.domainDim() );
        boxSide bs(1);
        bcInfo.addCondition(0, bs, condition_type::neumann, &g, 0);

        m_bcInfo = bcInfo;
        */
    }

public:

    /** \brief Computes the error estimate.
     *
     * Computes the residual-based error estimate \f$\eta\f$
     * (see class-documentation at the top).
     *
     *
     * \param storeElWise Bool indicating whether the element-wise
     * errors should be stored also. If <em>storeEletWise = true</em>,
     * the gsVector of element-wise estimates \f$\eta_K^2\f$
     * can be obtained by
     * calling elementNorms().
     *
     * \returns The total estimated error \f$ \eta \f$.
     *
     */
    T compute(bool storeElWise = true)
    {
        this->apply(*this,storeElWise);
        return this->m_value;
    }


protected:

    /**
     * @brief Initializes the error estimator
     *
     * Sets up the quadrature rule (based on the degree of \em basis) and
     * the \em evFlags for the gsGeometryEvaluator that are needed
     * for this specific problem.
     *
     * \param[in] basis
     * \param[out] rule
     * \param[out] evFlags
     */
    void initialize(const gsBasis<T> & basis,
                    gsQuadRule<T> & rule,
                    unsigned      & evFlags) // replace with geoEval ?
    {
        m_parDim = basis.dim();

        GISMO_ASSERT(m_parDim == 2 || m_parDim == 3, "Called error estimator with dimension other than 2 or 3.");

        // Setup Quadrature
        gsVector<index_t> numQuadNodes( m_parDim );
        for (unsigned i = 0; i < m_parDim; ++i)
            numQuadNodes[i] = basis.degree(i) + 1;

        rule = gsGaussRule<T>(numQuadNodes);// harmless slicing occurs here

        // Set Geometry evaluation flags
        // is used in evaluate()
        evFlags = NEED_MEASURE| NEED_VALUE| NEED_JACOBIAN | NEED_2ND_DER | NEED_GRAD_TRANSFORM;
    }

    // Evaluate on element.
    /**
     * @brief Evaluate some needed data on the given quadrature nodes
     *
     * Executes and stores needed function evaluations at \em quNodes.\n
     * The gsGeometryEvaluator \em geoEval is also evaluated at the nodes,
     * using evaluation flags specified in initialize().
     *
     * @param[in,out] geoEval
     * @param[in] discSolution
     * @param[in] rhsFunction
     * @param[in] quNodes
     */
    inline void evaluate(gsGeometryEvaluator<T> & geoEval,
                         const gsGeometry<T>    & discSolution,
                         const gsFunction<T>    & rhsFunction,
                         gsMatrix<T>            & quNodes)
    {
        // Evaluate discrete solution
        discSolution.deriv2_into(quNodes, m_discSol2ndDer);

        // Compute geometry related values
        geoEval.evaluateAt(quNodes);

        // Evaluate right-hand-side function (defined of physical domain)
        rhsFunction.eval_into(geoEval.values(), m_rhsFctVals);
    }

    // assemble on element



    /**
     * @brief Computes the local error estimate on an element.
     *
     * See documentation of the class for the computed error estimate.
     *
     * @param[in] element specifies the element \f$ K \f$.
     * @param[in] geoEval gsGeometryEvaluator as evaluated in evaluate().
     * @param[in] quWeights Quadrature weights \em before transformation the the
     * element, i.e., the sum of the weights should be 1.
     * @return The \em squared estimate \f$ \eta_K^2 \f$ of local error on
     * element \f$ K \f$.

     */
    inline T compute(gsDomainIterator<T>    & element,
                     gsGeometryEvaluator<T> & geoEval,
                     gsVector<T> const      & quWeights)
    {

        unsigned actPatch = geoEval.id();


        T sumVolSq(0.0);
        T sumSidesSq(0.0);
        gsMatrix<T> quNodesSide;
        gsVector<T> quWeightsSide;

        // will be used to set up quadrature points on the side
        gsVector<index_t> numQuadNodesSide;
        // reference-vector with default numbers of quadrature points
        gsVector<index_t> numQuadNodesRef( m_parDim );
        for (unsigned i = 0; i < m_parDim; ++i)
            numQuadNodesRef[i] = patchesPtr->basis(actPatch).degree(i) + 1;

        for (index_t k = 0; k < quWeights.size(); ++k) // loop over quadrature nodes
        {
            const T weight = quWeights[k] * geoEval.measure(k);

            const typename gsMatrix<T>::constColumns J = geoEval.jacobian(k);
            gsMatrix<T> sol_der2 = m_discSol2ndDer.col(k);

            // Compute the APPROXIMATION of the
            // transformation of the
            // Laplacian to the physical domain.
            // Note that the term involving the second
            // derivative of the inverse geometry mapping is
            // neglected!
            //
            // The transformation is written here explicitly,
            // because of the special ordering of the second derivatives,
            // and because it should be easier to extend this to the
            // convection-diffusion-reaction-equation starting from this.

            T sol_Lap(0.0);

            if( m_parDim == 2 )
            {
                gsMatrix<T> Jinv = J.inverse();

                for( unsigned i=0; i < m_parDim; i ++ )
                {
                    sol_Lap += sol_der2(0,0) * Jinv(0,i) * Jinv(0,i) \
                        + sol_der2(2,0) * Jinv(0,i) * Jinv(1,i) \
                        + sol_der2(2,0) * Jinv(1,i) * Jinv(0,i) \
                        + sol_der2(1,0) * Jinv(1,i) * Jinv(1,i);
                }
            }
            else if( m_parDim == 3 )
            {
                gsMatrix<T> Jinv = J.inverse();

                for( unsigned i=0; i < m_parDim; i ++ )
                {
                    sol_Lap += \
                          sol_der2(0,0) * Jinv(0,i) * Jinv(0,i) \
                        + sol_der2(3,0) * Jinv(0,i) * Jinv(1,i) \
                        + sol_der2(4,0) * Jinv(0,i) * Jinv(2,i) \
                        + sol_der2(3,0) * Jinv(1,i) * Jinv(0,i) \
                        + sol_der2(1,0) * Jinv(1,i) * Jinv(1,i) \
                        + sol_der2(5,0) * Jinv(1,i) * Jinv(2,i) \
                        + sol_der2(4,0) * Jinv(2,i) * Jinv(0,i) \
                        + sol_der2(5,0) * Jinv(2,i) * Jinv(1,i) \
                        + sol_der2(2,0) * Jinv(2,i) * Jinv(2,i);
                }
            }

            // residual squared: Laplace of solution + RHS.
            sumVolSq += weight * ( sol_Lap + m_rhsFctVals(0,k) ) \
                    * ( sol_Lap + m_rhsFctVals(0,k) );

        } // quPts volume


        // find out which boundaries are touched by this particular element
        std::vector<unsigned> touchingSides;
        for( size_t di = 0; di < m_parDim; di++)
        {
            if( element.lowerCorner()[di] == 0 )
                touchingSides.push_back(2*di+1);
            if( element.upperCorner()[di] == 1 )
                touchingSides.push_back(2*di+2);
        }

        // compute contributions for each side of the element
        // psi = "patchside i"
        sumSidesSq = T(0.0);
        for( size_t psi = 0; psi < touchingSides.size(); psi++ ) // todo: use getConditionsForPatch and iterate over them
        {
            boxSide bs( touchingSides[psi] );
            patchSide ps( int(actPatch), bs );

            boundaryInterface intfcRes;
            // isIntfc is true, if this patchside is an interface
            bool isIntfc = patchesPtr->getInterface( ps, intfcRes );
            // bc will be the null-pointer, if there is no boundary condition
            // on this patchside
            const boundary_condition<T> * bc = m_bcInfo.getConditionFromSide( ps );
            // Note that, if the side is NOT an interface, the case
            // (bc == NULL) corresponds to a homogenous Neumann-boundary condition.
            // Thus, the contribution of such a boundary also has to be computed.
            // The case bc == NULL is treated in the function diffNeumannBC()

            // create quadrature for the side of the element
            numQuadNodesSide = numQuadNodesRef;
            numQuadNodesSide[ int(  (touchingSides[psi]-1)/2 ) ] = 1;

            gsVector<T> loSide( element.lowerCorner() );
            gsVector<T> upSide( element.upperCorner() );

            switch( touchingSides[psi] )
            {
            case 1:
                upSide[0] = 0;
                break;
            case 2:
                loSide[0] = 1;
                break;
            case 3:
                upSide[1] = 0;
                break;
            case 4:
                loSide[1] = 1;
                break;
            case 5:
                upSide[2] = 0;
                break;
            case 6:
                loSide[2] = 1;
                break;
            }
            gsGaussRule<T> quRuleSide(numQuadNodesSide);
            quRuleSide.mapTo( loSide, upSide, quNodesSide, quWeightsSide );

            // treat the cases where the side is an interface or on the boundary
            if( isIntfc )
            {
                sumSidesSq += 0.5 * diffIntfc( * field1, geoEval, ps, intfcRes, quNodesSide, quWeightsSide );
            }
            else
            {
                // if bc is the null-pointer, the side will be considered a free surface, i.e.,
                // one with homogenous Neumann bondary conditions.
                sumSidesSq += diffNeumannBC( * field1, geoEval, ps, bc, quNodesSide, quWeightsSide );
            }

        } // psi


        // Estimate the cell-size on the physical domain
        T hhSq = cellsizeEstimateSquared( element, geoEval );

        return hhSq * sumVolSq + math::sqrt( hhSq ) * sumSidesSq;
    }



    /**
     * @brief Computes the contribution from jumps of the derivative across interfaces.
     *
     * Computes the value\n
     * \f$ \int_{\partial K \cap \partial \Omega'} ( \partial_n u_h - \partial_n u_h' )^2 ds, \f$\n
     * where\n
     * \f$ \vec{n} \f$ is the outer unit-normal vector to the current patch,\n
     * \f$ \partial_n u = \nabla u \cdot \vec{n} \f$ is the normal derivative,\n
     * \f$ u_h' \f$ is the discrete solution on a neighbouring patch \f$ \Omega' \f$, and\n
     * \f$ \partial K \cap \partial \Omega' \f$ denotes the intersection of the bondary of the
     * current element with the boundary of the neighbouring patch.
     *
     * @param solField
     * @param geoEval
     * @param ps
     * @param intfc
     * @param quNodes
     * @param quWeights
     * @return The value described above.

     *
     */
    inline T diffIntfc( const gsField<T> & solField,
                        gsGeometryEvaluator<T> & geoEval,
                        patchSide & ps,
                        boundaryInterface & intfc,
                        gsMatrix<T> & quNodes,
                        const gsVector<T> & quWeights )
    {
        const size_t d = size_t( quNodes.rows() );

        patchSide p1 = intfc.first();
        patchSide p2 = intfc.second();
        patchSide psNeigh;

        ( p1.patch == ps.patch ? psNeigh = p2 : psNeigh = p1 );

        // get the points corresponding to the quNodes on the neighbour
        gsMatrix<T> quNodesNeigh( quNodes );
        quNodesNeigh.setZero();
        for( size_t k=0; k < size_t( quNodes.cols() ); k++)
            for( size_t i=0; i < d; i++ )
            {
                quNodesNeigh( intfc.dirMap(ps,i), k ) = quNodes(i,k);
                if( !intfc.dirOrientation(ps,i) )
                    quNodesNeigh( intfc.dirMap(ps,i), k) = T(1.0) - quNodesNeigh( intfc.dirMap(ps,i), k);
            }


        // (create and) evaluate the geometryEvaluators on the quNodes
        geoEval.evaluateAt(quNodes);

        typename gsGeometry<T>::Evaluator geoEvalNeigh( patchesPtr->patch( psNeigh.patch ).evaluator( geoEval.getFlags() ));
        geoEvalNeigh->evaluateAt( quNodesNeigh );

        // compute the gradients on both patches
        gsMatrix<T> grads;
        gsMatrix<T> gradsNeigh;
        gsMatrix<T> trf_grads;
        gsMatrix<T> trf_gradsNeigh;

        solField.igaFunction( ps.patch      ).deriv_into( quNodes,      grads );
        solField.igaFunction( psNeigh.patch ).deriv_into( quNodesNeigh, gradsNeigh );

        gsVector<T> outerNorm;

        T sum(0.0);
        // quNodes
        for( size_t qk = 0; qk < size_t( quNodes.cols() ); qk++)
        {
            const T weight = quWeights[qk] * geoEval.measure(qk);

            // compute normal vector and normalize to 1
            geoEval.outerNormal( qk, ps.side(), outerNorm );
            outerNorm.normalize();

            // transform the gradients
            geoEval.transformGradients(      qk, grads, trf_grads );
            geoEvalNeigh->transformGradients( qk, gradsNeigh, trf_gradsNeigh );

            T tmp(0.0);
            T tmpNeigh(0.0);
            for( size_t j = 0; j < d; j++)
            {
                tmp += trf_grads(j,0) * outerNorm(j,0);
                tmpNeigh += trf_gradsNeigh(j,0) * outerNorm(j,0);
            }
            sum += weight * ( tmp - tmpNeigh )*( tmp - tmpNeigh );

        } // quNodes

        return sum;
    }


    /**
     * @brief Computes the contribution from the Neumann boundary.
     *
     ** Computes the value\n
     * \f$ \int_{\partial K \cap \Gamma_N} ( g_N - \partial_n u_h )^2 ds, \f$\n
     * where\n
     * \f$ \vec{n} \f$ is the outer unit-normal vector to the current patch,\n
     * \f$ \partial_n u = \nabla u \cdot \vec{n} \f$ is the normal derivative,\n
     * \f$ g_N \f$ is the given Neumann data.
     *
     * @param solField
     * @param geoEval
     * @param ps
     * @param bc
     * @param quNodes
     * @param quWeights
     * @return The value described above.
     */
    inline T diffNeumannBC( const gsField<T> & solField,
                        gsGeometryEvaluator<T> & geoEval,
                        patchSide & ps,
                        const boundary_condition<T> * bc ,
                        gsMatrix<T> & quNodes,
                        const gsVector<T> & quWeights )
    {
        const size_t d = quNodes.rows();

        // (create and) evaluate the geometryEvaluators on the quNodes
        geoEval.evaluateAt(quNodes);

        // compute the gradients
        gsMatrix<T> grads;
        gsMatrix<T> trf_grads;

        solField.igaFunction( ps.patch ).deriv_into( quNodes, grads );

        gsVector<T> outerNorm;

        T sum(0.0);
        // quNodes
        for( size_t qk = 0; qk < size_t( quNodes.cols() ); qk++)
        {
            const T weight = quWeights[qk] * geoEval.measure(qk);

            // compute normal vector and normalize to 1
            geoEval.outerNormal( qk, ps.side(), outerNorm );
            outerNorm.normalize();

            // transform the gradients
            geoEval.transformGradients( qk, grads, trf_grads );

            if( bc == NULL )
            {
                // it is assumed that the function is only called for patch-sides
                // which are NOT patch-interfaces.
                // If it is neither patch-interface nor a specified boundary-side,
                // it is considered a traction boundary with homogenous boundary conditions,
                // i.e., a free boundary

                T tmp(0.0);
                for( size_t j = 0; j < d; j++)
                    tmp += trf_grads(j,0) * outerNorm(j,0);
                sum += weight * tmp * tmp;
            }
            else if( bc->type() == condition_type::neumann )
            {
                gsDebugVar(bc->type());
                gsMatrix<T> bcFct;
                bc->function()->eval_into( quNodes.col(qk), bcFct );
                int unk = bc->unknown();


                T tmp(0.0);
                for( size_t j = 0; j < d; j++)
                    tmp += trf_grads(j,0) * outerNorm(j,0);

                sum += weight * ( tmp - bcFct(unk,0) ) * ( tmp - bcFct(unk,0) );
            }
        } // quNodes

        return sum;
    }


    /**
     * @brief Estimates the size of the cell in the physical domain.
     *
     * The estimate is computed by mapping the corners of the cell to
     * the physical domain and comparing the distances between all
     * corners.
     *
     * As long as the cells are not distorted too extremely,
     * this should provide a useful estimate of the diameter.
     *
     * @param element
     * @param geoEval
     * @return the estimated diameter of the cell in the physical space.
     */
    inline T cellsizeEstimateSquared( const gsDomainIterator<T> & element, gsGeometryEvaluator<T> & geoEval ) const
    {
        T hhSq = T(0.0);
        index_t idxMax(0);

        if( m_parDim == 2 )
        {
            idxMax = 4;
            gsMatrix<T> corners(2,idxMax);

            corners << element.lowerCorner()[0] , element.lowerCorner()[0] , \
                    element.upperCorner()[0] , element.upperCorner()[0] , \
                    element.lowerCorner()[1] , element.upperCorner()[1] , \
                    element.lowerCorner()[1] , element.upperCorner()[1];

            geoEval.evaluateAt(corners);
        }
        else if( m_parDim == 3 )
        {
            idxMax = 8;
            gsMatrix<T> corners(3,idxMax);

            corners << element.lowerCorner()[0] , \
                    element.lowerCorner()[0] , \
                    element.lowerCorner()[0] , \
                    element.lowerCorner()[0] , \
                    element.upperCorner()[0] , \
                    element.upperCorner()[0] , \
                    element.upperCorner()[0] , \
                    element.upperCorner()[0] , \
                    element.lowerCorner()[1] , \
                    element.lowerCorner()[1] , \
                    element.upperCorner()[1] , \
                    element.upperCorner()[1] , \
                    element.lowerCorner()[1] , \
                    element.lowerCorner()[1] , \
                    element.upperCorner()[1] , \
                    element.upperCorner()[1] , \
                    element.lowerCorner()[2] , \
                    element.upperCorner()[2] , \
                    element.lowerCorner()[2] , \
                    element.upperCorner()[2] , \
                    element.lowerCorner()[2] , \
                    element.upperCorner()[2] , \
                    element.lowerCorner()[2] , \
                    element.upperCorner()[2];

            geoEval.evaluateAt(corners);
        }
        else
        {
            GISMO_ASSERT(false,"only implemented for dimension 2 or 3");
        }

        gsMatrix<T> pC = geoEval.values();

        for( index_t i = 0; i < idxMax; i++)
            for( index_t j = (i+1); j < idxMax; j++)
            {
                T d(0.0);
                for( index_t di = 0; di < index_t( m_parDim ); di++)
                    d += ( pC( di, i)-pC( di, j) )*( pC( di, i)-pC( di, j) );

                if( d > hhSq )
                    hhSq = d;
            }
        return hhSq;
    }

private:

    gsBoundaryConditions<T> m_bcInfo;

    gsMatrix<T> m_discSol2ndDer;
    gsMatrix<T> m_rhsFctVals;
    gsMatrix<T> m_phHessVals;
    unsigned m_parDim;

    bool m_f2param;

    using gsNorm<T>::patchesPtr;
    using gsNorm<T>::field1;

    //unsigned evFlags;
};


} // namespace gismo

