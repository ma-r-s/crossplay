"""The TeX renderer: formulas as linear text, and the leaves that find the
dump's words. Every row is a real shape from the essentials."""

import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

import article_html as ah  # noqa: E402
import tex_text  # noqa: E402


class Render(unittest.TestCase):
    def test_shapes(self):
        cases = [
            (r"J_{z}^{0}=-J_{z}^{1}", "J_z⁰ = −J_z¹"),
            (r"L^{2},S^{2},J^{2}", "L², S², J²"),
            (r"K_{0}", "K₀"),
            (r"ev(i,j)=f_{i}(j)", "ev(i, j) = f_i(j)"),
            (r"\mathbb {Z} /n\mathbb {Z}", "Z/nZ"),
            (r"\mathbf {S} =\left(S_{x},S_{y},S_{z}\right)", "S = (S_x, S_y, S_z)"),
            (r"x={\frac {-b\pm {\sqrt {b^{2}-4ac}}}{2a}}", "x = (−b±sqrt(b²−4ac))/2a"),
            (r"\sum _{i=1}^{n}x_{i}^{2}", "sum from i = 1 to n of x_i²"),
            (r"\int _{a}^{b}f(x)\,dx", "integral from a to b of f(x)dx"),
            (r"\lim _{x\to 0}{\frac {\sin x}{x}}=1", "lim as x -> 0 of (sin x)/x = 1"),
            (r"{\begin{pmatrix}a&b\\c&d\end{pmatrix}}", "(a, b; c, d)"),
            (r"f(x)={\begin{cases}1&{\text{if }}x>0\\0&{\text{otherwise}}\end{cases}}", "f(x) = { 1 if x > 0; 0 otherwise }"),
            (r"2\not \equiv 4{\pmod {4}}", "2 !== 4 (mod 4)"),
            (r"\left.{\frac {\partial C}{\partial x}}\right|_{x=z}", "dC/dx|_(x = z)"),
            (r"e^{i\pi }+1=0", "e^(i pi) + 1 = 0"),
            (r"90^{\circ }", "90°"),
            (r"\overline{z_{0}}", "z₀-bar"),
            (r"\hat{H}\psi =E\psi", "H-hat psi = E psi"),
            (r"P_{\text{in}}/4\pi", "P_in/4 pi"),
            (r"L_{z}=-i\hbar {\frac {\partial }{\partial \phi }}", "L_z = −ih-bar d/(d phi)"),
            (r"\tau _{*}={\frac {u_{*}^{2}}{(s-1)gd}}", "tau_* = (u_*²)/((s−1)gd)"),
            (r"a\leq b\neq c\approx d\in S\subseteq T", "a <= b != c ~ d in S subset of T"),
            (r"\alpha +\beta =\gamma", "alpha + beta = gamma"),
            (r"{\text{gain-db}}=10\log _{10}\left({\frac {P_{\text{out}}}{P_{\text{in}}}}\right)~{\text{dB}}", "gain-db = 10 log₁₀((P_out)/(P_in)) dB"),
            (r"\binom {n}{k}", "C(n, k)"),
            (r"\sqrt[3]{x}", "root(3, x)"),
            (r"x_{i+1}", "x_(i + 1)"),
            (r"x_{i}\in X_{i}{\text{ for every }}i\in \{1,\dots ,n\}", "x_i in X_i for every i in {1, ..., n}"),
        ]
        for tex, want in cases:
            text, complete = tex_text.render(tex)
            self.assertEqual(text, want, tex)
            self.assertTrue(complete, tex)

    def test_unknown_command_is_incomplete(self):
        text, complete = tex_text.render(r"{a \over b}")
        self.assertFalse(complete)
        text, complete = tex_text.render(r"\ce {H2O}")
        self.assertFalse(complete)

    def test_leaves_match_the_dumps_words(self):
        for tex, words in [
            (r"J_{z}^{0}=-J_{z}^{1}", "J z 0 = − J z 1"),
            (r"\tau _{*}={\frac {u_{*}^{2}}{(s-1)gd}}", "τ ∗ = u ∗ 2 (s − 1) g d"),
            (r"{\text{gain-db}}=10\log _{10}\left({\frac {P_{\text{out}}}{P_{\text{in}}}}\right)~{\text{dB}},", "gain-db = 10 log 10 (P out P in) dB,"),
            (r"2\not \equiv 4{\pmod {4}}", "2 ≢ 4 (mod 4)"),
            (r"\mathbf {AA} ={\begin{bmatrix}1&2\\3&4\end{bmatrix}},", "A A =,"),
        ]:
            skip = "matrix" in tex
            self.assertEqual(tex_text.loose(tex_text.leaves(tex, skip_matrices=skip)), tex_text.loose(words), tex)


class Integration(unittest.TestCase):
    def test_words_replaced_by_the_rendering(self):
        src = ("Then using J z 0 = − J z 1 {\\displaystyle J_{z}^{0}=-J_{z}^{1}} and L 2, S 2, J 2 "
               "{\\displaystyle L^{2},S^{2},J^{2}} we get K 0 {\\displaystyle K_{0}}.")
        self.assertEqual(ah.clean_text(src), "Then using J_z⁰ = −J_z¹ and L², S², J² we get K₀.")

    def test_words_stay_when_unmatched_or_incomplete(self):
        self.assertEqual(ah.clean_text("the value x y {\\displaystyle {a \\over b}} here"), "the value x y here")
        self.assertEqual(ah.clean_text("some other words {\\displaystyle J_{z}} here"), "some other words here")

    def test_matrix_words_replaced(self):
        src = "A A =, {\\displaystyle \\mathbf {AA} ={\\begin{bmatrix}1&2\\\\3&4\\end{bmatrix}},}"
        self.assertEqual(ah.clean_text(src), "AA = [1, 2; 3, 4],")


if __name__ == "__main__":
    unittest.main()
