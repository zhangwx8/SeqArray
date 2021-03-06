\name{seqApply}
\alias{seqApply}
\title{Apply Functions Over Array Margins}
\description{
    Returns a vector or list of values obtained by applying a function to
margins of genotypes and annotations.
}
\usage{
seqApply(gdsfile, var.name, FUN, margin=c("by.variant", "by.sample"),
    as.is=c("none", "list", "integer", "double", "character", "logical", "raw"),
    var.index=c("none", "relative", "absolute"), parallel=FALSE,
    .useraw=FALSE, .progress=FALSE, .list_dup=TRUE, ...)
}
\arguments{
    \item{gdsfile}{a \code{\link{SeqVarGDSClass}} object}
    \item{var.name}{the variable name(s), see details}
    \item{FUN}{the function to be applied}
    \item{margin}{giving the dimension which the function will be applied over}
    \item{as.is}{returned value: a list, an integer vector, etc;
        \code{as.is} can be a \code{\link{connection}} object, or a GDS node
        \code{\link{gdsn.class}} object; if "unlist" is used, produces a vector
        which contains all the atomic components, via
        \code{unlist(..., recursive=FALSE)}}
    \item{var.index}{if \code{"none"}, call \code{FUN(x, ...)} without variable
        index; if \code{"relative"} or \code{"absolute"}, add an argument to
        the user-defined function \code{FUN} like \code{FUN(index, x, ...)}
        where \code{index} is an index of variant starting from 1 if
        \code{margin = "by.variant"}: \code{"relative"} for indexing in the
        selection defined by \code{\link{seqSetFilter}}, \code{"absolute"}
        for indexing with respect to all data}
    \item{parallel}{\code{FALSE} (serial processing), \code{TRUE} (multicore
        processing), numeric value or other value; \code{parallel} is passed
        to the argument \code{cl} in \code{\link{seqParallel}}, see
        \code{\link{seqParallel}} for more details.}
    \item{.useraw}{\code{TRUE}, force to use RAW instead of INTEGER for
        genotypes and dosages; \code{FALSE}, use INTEGER; \code{NA}, use RAW
        for small numbers instead of INTEGER if possible, it is needed to
        detect data type (RAW or INTEGER) in the user-defined function;
        for genotypes, 0xFF is missing value if RAW is used}
    \item{.progress}{if \code{TRUE}, show progress information}
    \item{.list_dup}{internal use only}
    \item{...}{optional arguments to \code{FUN}}
}
\details{
    The variable name should be \code{"sample.id"}, \code{"variant.id"},
\code{"position"}, \code{"chromosome"}, \code{"allele"}, \code{"genotype"},
\code{"annotation/id"}, \code{"annotation/qual"}, \code{"annotation/filter"},
\code{"annotation/info/VARIABLE_NAME"}, or
\code{"annotation/format/VARIABLE_NAME"}.

\code{"@genotype"}, \code{"annotation/info/@VARIABLE_NAME"} or
\code{"annotation/format/@VARIABLE_NAME"} are used to obtain the index
associated with these variables.

\code{"$dosage"} is also allowed for the dosages of reference allele (integer:
0, 1, 2 and NA for diploid genotypes).

\code{"$dosage_alt"} returns a RAW/INTEGER matrix for the dosages of alternative
allele without distinguishing different alternative alleles.

\code{"$num_allele"} returns an integer vector with the numbers of distinct
alleles.

\code{"$ref"} returns a character vector of reference alleles

\code{"$alt"} returns a character vector of alternative alleles (delimited by
comma)

\code{"$chrom_pos"} returns characters with the combination of chromosome and
position, e.g., "1:1272721". \code{"$chrom_pos_allele"} returns characters with
the combination of chromosome, position and alleles, e.g., "1:1272721_A_G"
(i.e., chr:position_REF_ALT).

    The algorithm is highly optimized by blocking the computations to exploit
the high-speed memory instead of disk.
}
\value{
    A vector, a list of values or none.
}

\author{Xiuwen Zheng}
\seealso{
    \code{\link{seqBlockApply}}, \code{\link{seqSetFilter}},
    \code{\link{seqGetData}}, \code{\link{seqParallel}},
    \code{\link{seqGetParallel}}
}

\examples{
# the GDS file
(gds.fn <- seqExampleFileName("gds"))

# display
(f <- seqOpen(gds.fn))

# get 'sample.id
(samp.id <- seqGetData(f, "sample.id"))
# "NA06984" "NA06985" "NA06986" ...

# get 'variant.id'
head(variant.id <- seqGetData(f, "variant.id"))


# set sample and variant filters
set.seed(100)
seqSetFilter(f, sample.id=samp.id[c(2,4,6,8,10)],
    variant.id=sample(variant.id, 10))

# read
seqApply(f, "genotype", FUN=print, margin="by.variant")
seqApply(f, "genotype", FUN=print, margin="by.variant", .useraw=TRUE)

seqApply(f, "genotype", FUN=print, margin="by.sample")
seqApply(f, "genotype", FUN=print, margin="by.sample", .useraw=TRUE)


# read multiple variables variant by variant
seqApply(f, c(geno="genotype", phase="phase", rsid="annotation/id",
    DP="annotation/format/DP"), FUN=print, as.is="none")

# get the numbers of alleles per variant
seqApply(f, "allele",
    FUN=function(x) length(unlist(strsplit(x,","))), as.is="integer")

# output to a file
fl <- file("tmp.txt", "wt")
seqApply(f, "genotype", FUN=sum, na.rm=TRUE, as.is=fl)
close(fl)
readLines("tmp.txt")

seqApply(f, "genotype", FUN=sum, na.rm=TRUE, as.is=stdout())
seqApply(f, "genotype", FUN=sum, na.rm=TRUE, as.is="integer")
# should be identical



################################################################
# with an index of variant

seqApply(f, c(geno="genotype", phase="phase", rsid="annotation/id"),
    FUN=function(index, x) { print(index); print(x); index },
    as.is="integer", var.index="relative")
# it is as the same as
which(seqGetFilter(f)$variant.sel)



################################################################
# reset sample and variant filters
seqResetFilter(f)

# calculate the frequency of reference allele,
#   a faster version could be obtained by C coding
af <- seqApply(f, "genotype", FUN=function(x) mean(x==0L, na.rm=TRUE),
    as.is="double")
length(af)
summary(af)



################################################################
# apply the user-defined function sample by sample

# reset sample and variant filters
seqResetFilter(f)
summary(seqApply(f, "genotype", FUN=function(x) { mean(is.na(x)) },
    margin="by.sample", as.is="double"))

# set sample and variant filters
set.seed(100)
seqSetFilter(f, sample.id=samp.id[c(2,4,6,8,10)],
    variant.id=sample(variant.id, 10))

seqApply(f, "genotype", FUN=print, margin="by.variant", as.is="none")

seqApply(f, "genotype", FUN=print, margin="by.sample", as.is="none")

seqApply(f, c(sample.id="sample.id", genotype="genotype"), FUN=print,
    margin="by.sample", as.is="none")


# close the GDS file
seqClose(f)


# delete the temporary file
unlink("tmp.txt")
}

\keyword{gds}
\keyword{sequencing}
\keyword{genetics}
