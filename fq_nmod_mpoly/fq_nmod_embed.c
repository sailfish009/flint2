/*
    Copyright (C) 2018 Daniel Schultz

    This file is part of FLINT.

    FLINT is free software: you can redistribute it and/or modify it under
    the terms of the GNU Lesser General Public License (LGPL) as published
    by the Free Software Foundation; either version 2.1 of the License, or
    (at your option) any later version.  See <http://www.gnu.org/licenses/>.
*/

#include "fq_nmod_mpoly.h"

/*
    F_q is the "small" field presented as F_p[theta]/f(theta)
    where q = p^m and f is a polynomial over F_p of degree m

    We would like to extend this small field to a "large" field F_q^n
    presented as F_p[phi]/g(phi) where g is a polynomial over F_p of degree m*n

    F_q^n is then ismorphic to F_p[theta][x]/(f(theta), h(theta, x)) for
    some h of degree n in x.

    we compute h and values
        x       as an element of F_p[phi]/g(phi)
        theta   as an element of F_p[phi]/g(phi)
        phi     as an element of F_p[theta][x]

Example:
    with theta printed as # as phi as $, the two embeddings for a
    degree 8 extension F_{3^16} = F_3/($^16+$^10+2) of F_9 = F_3[#]/(#^2+#+2)
    could be:

**** emb[0]:
p: 3
m: 2
n: 8
sm modulus: #^2+#+2
lg modulus: $^16+$^10+2
h: x^8+(2*#+1)*x^6+x^4+(#+1)*x^2+(2*#)
  phi $: x
theta #: 2*$^14+$^10+$^6+$^4+2*$^2+1
      x: $
**** emb[1]:
p: 3
m: 2
n: 8
sm modulus: #^2+#+2
lg modulus: $^16+$^10+2
h: x^8+(#+2)*x^6+x^4+(2*#)*x^2+(#+1)
  phi $: x
theta #: $^14+2*$^10+2*$^6+2*$^4+$^2+1
      x: $
*/


/* the polynmial h is printed with variable x */
/*
static void _embed_print(const fq_nmod_embed_t emb)
{
flint_printf("p: %wu\n", emb->smctx->modulus->mod.n);
flint_printf("m: %wd\n", nmod_poly_degree(emb->smctx->modulus));
flint_printf("n: %wd\n", fq_nmod_poly_degree(emb->h, emb->smctx));

printf("sm modulus: "); nmod_poly_print_pretty(emb->smctx->modulus, emb->smctx->var); printf("\n");
printf("lg modulus: "); nmod_poly_print_pretty(emb->lgctx->modulus, emb->lgctx->var); printf("\n");
printf("h: "); fq_nmod_poly_print_pretty(emb->h, "x", emb->smctx); printf("\n");

printf("  phi %s: ",emb->lgctx->var); fq_nmod_poly_print_pretty(emb->phi_sm, "x", emb->smctx); printf("\n");
printf("theta %s: ",emb->smctx->var); fq_nmod_print_pretty(emb->theta_lg, emb->lgctx); printf("\n");
printf("      x: ");                  fq_nmod_print_pretty(emb->x_lg, emb->lgctx); printf("\n");
}
*/

void _fq_nmod_embed_clear(_fq_nmod_embed_t emb)
{
    fq_nmod_poly_clear(emb->phi_sm, emb->smctx);
    fq_nmod_poly_clear(emb->h, emb->smctx);

    fq_nmod_clear(emb->theta_lg, emb->lgctx);
    fq_nmod_clear(emb->x_lg, emb->lgctx);
}

void _fq_nmod_embed_array_clear(_fq_nmod_embed_struct * emb, slong m)
{
    slong i;
    for (i = 0; i < m; i++)
        _fq_nmod_embed_clear(emb + i);
}
/*
    initialize an array of m embeddings making an extension of degree n
*/
void _fq_nmod_embed_array_init(_fq_nmod_embed_struct * emb,
                      const fq_nmod_ctx_t bigctx, const fq_nmod_ctx_t smallctx)
{
    slong i, j, k, l;
    _fq_nmod_embed_struct * cur;
    fq_nmod_poly_t poly;
    fq_nmod_t t;
    fq_nmod_poly_t poly2;
    fq_nmod_t t2, lead2;
    fq_nmod_t t3;
    fq_nmod_poly_factor_t fac2;
    nmod_mat_t M, Msol;
    fq_nmod_t biggen;
    fmpz_t P;
    mp_limb_t lc_inv;
    slong nullity;
    mp_limb_t p = smallctx->modulus->mod.n;
    slong n, m = nmod_poly_degree(smallctx->modulus);

    /* n is the degree of the extension */
    n = nmod_poly_degree(bigctx->modulus);
    FLINT_ASSERT((n%m) == 0);
    n = n/m;

    fmpz_init_set_ui(P,p);

    if (m == 1)
    {
        cur = emb + 0;
        cur->smctx = smallctx;
        cur->lgctx = bigctx;

        fq_nmod_init(cur->theta_lg, bigctx);
        FLINT_ASSERT(1 == nmod_poly_get_coeff_ui(cur->smctx->modulus, 1));
        fq_nmod_set_ui(cur->theta_lg, nmod_poly_get_coeff_ui(cur->smctx->modulus, 0), bigctx);
        fq_nmod_neg(cur->theta_lg, cur->theta_lg, bigctx);
        
        fq_nmod_init(cur->x_lg, bigctx);
        fq_nmod_gen(cur->x_lg, bigctx);

        fq_nmod_poly_init(cur->phi_sm, smallctx);
        fq_nmod_poly_gen(cur->phi_sm, smallctx);

        fq_nmod_poly_init(cur->h, smallctx);
        fq_nmod_init(t, smallctx);
        for (i = 0; i < nmod_poly_length(bigctx->modulus); i++)
        {
            fq_nmod_set_ui(t, nmod_poly_get_coeff_ui(bigctx->modulus, i), smallctx);
            fq_nmod_poly_set_coeff(cur->h, i, t, smallctx);
        }
/*
flint_printf("**** emb[0]:\n"); _embed_print(emb + 0);
*/
        fq_nmod_clear(t, smallctx);
        fmpz_clear(P);
        return;
    }

    /* poly will be bigctx->modulus as a polynomial over smallctx */
    fq_nmod_poly_init(poly, smallctx);
    fq_nmod_init(t, smallctx);
    for (i = 0; i < nmod_poly_length(bigctx->modulus); i++)
    {
        fq_nmod_set_ui(t, nmod_poly_get_coeff_ui(bigctx->modulus, i), smallctx);
        fq_nmod_poly_set_coeff(poly, i, t, smallctx);
    }

    /* poly2 will be smallctx->modulus as a polynomial over bigctx */
    fq_nmod_poly_init(poly2, bigctx);
    fq_nmod_init(t2, bigctx);
    fq_nmod_init(t3, bigctx);
    for (i = 0; i < nmod_poly_length(smallctx->modulus); i++)
    {
        fq_nmod_set_ui(t2, nmod_poly_get_coeff_ui(smallctx->modulus, i), bigctx);
        fq_nmod_poly_set_coeff(poly2, i, t2, bigctx);
    }

    /* poly2 should factor into m linear factors over bigctx*/
    fq_nmod_poly_factor_init(fac2, bigctx);
    fq_nmod_init(lead2, bigctx);
    fq_nmod_poly_factor(fac2, lead2, poly2, bigctx);
    FLINT_ASSERT(fac2->num == m);

    nmod_mat_init(M, m*n, m*n+1, p);
    nmod_mat_init(Msol, m*n+1, 1, p);

    fq_nmod_init(biggen, bigctx);
    fq_nmod_gen(biggen, bigctx);

    for (k = 0; k < m; k++)
    {
        cur = emb + k;
        cur->smctx = smallctx;
        cur->lgctx = bigctx;

        /* we will send x to phi and phi to x */
        fq_nmod_init(cur->x_lg, bigctx);
        fq_nmod_gen(cur->x_lg, bigctx);

        fq_nmod_poly_init(cur->phi_sm, smallctx);
        fq_nmod_poly_gen(cur->phi_sm, smallctx);

        /* theta is determined from a factor of poly2 */
        FLINT_ASSERT(fac2->exp[k] == 1);
        FLINT_ASSERT(fq_nmod_poly_degree(fac2->poly + k, bigctx) == 1);
        fq_nmod_init(cur->theta_lg, bigctx);
        fq_nmod_poly_get_coeff(cur->theta_lg, fac2->poly + k, 0, bigctx);
        fq_nmod_poly_get_coeff(t2, fac2->poly + k, 1, bigctx);
        fq_nmod_inv(t2, t2, bigctx);
        fq_nmod_neg(t2, t2, bigctx);
        fq_nmod_mul(cur->theta_lg, cur->theta_lg, t2, bigctx);

        /* determine h by a nullspace calculation */
        fq_nmod_one(t2, bigctx);
        for (i = 0; i < n; i++)
        {
            fq_nmod_set(t3, t2, bigctx);
            for (j = 0; j < m; j++)
            {
                FLINT_ASSERT(nmod_poly_degree(t3) < m*n);
                for (l = 0; l < m*n; l++)
                {
                    nmod_mat_entry(M, l, m*i + j) = nmod_poly_get_coeff_ui(t3, l);
                }
                fq_nmod_mul(t3, t3, cur->theta_lg, bigctx);
            }
            fq_nmod_mul(t2, t2, biggen, bigctx);
        }

        fq_nmod_pow_ui(t3, biggen, n, bigctx);
        for (l = 0; l < m*n; l++)
        {
            nmod_mat_entry(M, l, m*n) = nmod_poly_get_coeff_ui(t3, l);
        }
        nullity = nmod_mat_nullspace(Msol, M);
        FLINT_ASSERT(nullity == 1);

        /* this is the coefficient of x^n in h */
        FLINT_ASSERT(nmod_mat_entry(Msol, m*n, 0) != 0);
        lc_inv = nmod_inv(nmod_mat_entry(Msol, m*n, 0), smallctx->modulus->mod);

        /* set h now */
        fq_nmod_poly_init(cur->h, smallctx);
        for (i = 0; i < n; i++)
        {
            fq_nmod_zero(t, smallctx);
            for (j = 0; j < m; j++)
            {
                nmod_poly_set_coeff_ui(t, j, 
                            nmod_mul(lc_inv, nmod_mat_entry(Msol, m*i + j, 0),
                                 smallctx->modulus->mod));
            }
            fq_nmod_poly_set_coeff(cur->h, i, t, smallctx);
        }
        fq_nmod_set_ui(t, 1, smallctx);
        fq_nmod_poly_set_coeff(cur->h, n, t, smallctx);
/*
flint_printf("**** emb[%wd]:\n",k); _embed_print(emb + k);
*/
        /* the set of h's sould be the factorization of bigctx->modulus */
        FLINT_ASSERT(fq_nmod_poly_is_irreducible(cur->h, smallctx));
        FLINT_ASSERT(fq_nmod_poly_divides(poly, poly, cur->h, smallctx));
    }
    FLINT_ASSERT(fq_nmod_poly_degree(poly, smallctx) == 0);

    nmod_mat_clear(Msol);
    nmod_mat_clear(M);

    fq_nmod_clear(biggen, bigctx);

    fq_nmod_poly_clear(poly2, bigctx);
    fq_nmod_clear(t2, bigctx);
    fq_nmod_clear(t3, bigctx);

    fq_nmod_poly_factor_clear(fac2, bigctx);
    fq_nmod_clear(lead2, bigctx);

    fq_nmod_poly_clear(poly, smallctx);
    fq_nmod_clear(t, smallctx);

    fmpz_clear(P);
}


void _fq_nmod_embed_sm_to_lg(
    fq_nmod_t out,            /* element of lgctx */
    const fq_nmod_poly_t in,  /* poly over smctx */
    const _fq_nmod_embed_t emb)
{
    slong i, j;
    fq_nmod_poly_t inred;
    fq_nmod_t t1, t2;

    fq_nmod_poly_init(inred, emb->smctx);
    fq_nmod_poly_rem(inred, in, emb->h, emb->smctx);

    fq_nmod_init(t1, emb->lgctx);
    fq_nmod_init(t2, emb->lgctx);

    fq_nmod_zero(out, emb->lgctx);
    for (i = 0; i < fq_nmod_poly_length(inred, emb->smctx); i++)
    {
        nmod_poly_struct * coeff = inred->coeffs + i;
        fq_nmod_zero(t1, emb->lgctx);
        for (j = 0; j < nmod_poly_length(coeff); j++)
        {
            fq_nmod_pow_ui(t2, emb->theta_lg, j, emb->lgctx);
            fq_nmod_mul_ui(t2, t2, nmod_poly_get_coeff_ui(coeff, j), emb->lgctx);
            fq_nmod_add(t1, t1, t2, emb->lgctx);
        }
        fq_nmod_pow_ui(t2, emb->x_lg, i, emb->lgctx);
        fq_nmod_mul(t1, t1, t2, emb->lgctx);
        fq_nmod_add(out, out, t1, emb->lgctx);
    }
    fq_nmod_clear(t1, emb->lgctx);
    fq_nmod_clear(t2, emb->lgctx);
    fq_nmod_poly_clear(inred, emb->smctx);
}


void _fq_nmod_embed_lg_to_sm(
    fq_nmod_poly_t out,  /* poly over smctx */
    const fq_nmod_t in,  /* element of lgctx */
    const _fq_nmod_embed_t emb)
{
    slong i;
    fq_nmod_poly_t t1;
    fq_nmod_t t2;

    fq_nmod_poly_init(t1, emb->smctx);
    fq_nmod_init(t2, emb->smctx);

    fq_nmod_poly_zero(out, emb->smctx);
    for (i = 0; i < nmod_poly_length(in); i++)
    {
        fq_nmod_poly_pow(t1, emb->phi_sm, i, emb->smctx);
        fq_nmod_set_ui(t2, nmod_poly_get_coeff_ui(in, i), emb->smctx);
        fq_nmod_poly_scalar_mul_fq_nmod(t1, t1, t2, emb->smctx);
        fq_nmod_poly_add(out, out, t1, emb->smctx);
    }
    fq_nmod_poly_rem(out, out, emb->h, emb->smctx);

    fq_nmod_poly_clear(t1, emb->smctx);
    fq_nmod_clear(t2, emb->smctx);
}




_fq_nmod_embed_struct *
_fq_nmod_mpoly_embed_chooser_init(_fq_nmod_mpoly_embed_chooser_t embc,
                  fq_nmod_mpoly_ctx_t ectx, const fq_nmod_mpoly_ctx_t ctx,
                                                        flint_rand_t randstate)
{
    nmod_poly_t ext_modulus;
    fq_nmod_ctx_t ext_fqctx;
    mp_limb_t p = ctx->fqctx->modulus->mod.n;
    slong m = nmod_poly_degree(ctx->fqctx->modulus);
    slong n;

    n = WORD(20)/(m*FLINT_BIT_COUNT(p));
    n = FLINT_MAX(n, WORD(2));

    embc->p = p;
    embc->m = m;
    embc->n = n;

    embc->embed = (_fq_nmod_embed_struct *) flint_malloc(m*
                                                sizeof(_fq_nmod_embed_struct));

    /* init ectx with modulus of degree m*n */
    nmod_poly_init2(ext_modulus, p, m*n + 1);
    nmod_poly_randtest_sparse_irreducible(ext_modulus, randstate, m*n + 1);
    fq_nmod_ctx_init_modulus(ext_fqctx, ext_modulus, "$");
    fq_nmod_mpoly_ctx_init(ectx, ctx->minfo->nvars, ORD_LEX, ext_fqctx);
    fq_nmod_ctx_clear(ext_fqctx);
    nmod_poly_clear(ext_modulus);

    _fq_nmod_embed_array_init(embc->embed, ectx->fqctx, ctx->fqctx);

    embc->k = 0;
    return embc->embed + embc->k;
}

void
_fq_nmod_mpoly_embed_chooser_clear(_fq_nmod_mpoly_embed_chooser_t embc,
                  fq_nmod_mpoly_ctx_t ectx, const fq_nmod_mpoly_ctx_t ctx,
                                                        flint_rand_t randstate)
{
    _fq_nmod_embed_array_clear(embc->embed, embc->m);
    fq_nmod_mpoly_ctx_clear(ectx);
    flint_free(embc->embed);
}


_fq_nmod_embed_struct *
_fq_nmod_mpoly_embed_chooser_next(_fq_nmod_mpoly_embed_chooser_t embc,
                  fq_nmod_mpoly_ctx_t ectx, const fq_nmod_mpoly_ctx_t ctx,
                                                        flint_rand_t randstate)
{
    nmod_poly_t ext_modulus;
    fq_nmod_ctx_t ext_fqctx;
    mp_limb_t p = embc->p;
    slong m = embc->m;
    slong n = embc->n;

    embc->k++;
    if (embc->k < m)
        return embc->embed + embc->k;

    n++;
    embc->n = n;
    if (n > 1000)
        return NULL;

    _fq_nmod_embed_array_clear(embc->embed, embc->m);
    fq_nmod_mpoly_ctx_clear(ectx);

    /* init ectx with modulus of degree m*n */
    nmod_poly_init2(ext_modulus, p, m*n + 1);
    nmod_poly_randtest_sparse_irreducible(ext_modulus, randstate, m*n + 1);
    fq_nmod_ctx_init_modulus(ext_fqctx, ext_modulus, "$");
    fq_nmod_mpoly_ctx_init(ectx, ctx->minfo->nvars, ORD_LEX, ext_fqctx);
    fq_nmod_ctx_clear(ext_fqctx);
    nmod_poly_clear(ext_modulus);

    _fq_nmod_embed_array_init(embc->embed, ectx->fqctx, ctx->fqctx);

    embc->k = 0;
    return embc->embed + embc->k;
}
