#######################################################################
#
# Package Name: SeqArray
#
# Description: A list of units of selected variants
#


#######################################################################
# Get a list of units of selected variants via sliding windows based on basepairs
#
seqUnitSlidingWindows <- function(gdsfile, win.size=5000L, win.shift=2500L,
    win.start=0L, dup.rm=TRUE, verbose=TRUE)
{
    stopifnot(inherits(gdsfile, "SeqVarGDSClass"))
    stopifnot(is.numeric(win.size), is.finite(win.size), length(win.size)==1L,
        win.size>0L)
    stopifnot(is.numeric(win.shift), is.finite(win.shift), length(win.shift)==1L,
        win.shift>0L)
    stopifnot(is.numeric(win.start), is.finite(win.start), length(win.start)==1L)
    stopifnot(is.logical(dup.rm), length(dup.rm)==1L)
    stopifnot(is.logical(verbose), length(verbose)==1L)

    # save state
    seqSetFilter(gdsfile, action="push", verbose=FALSE)
    on.exit({ seqSetFilter(gdsfile, action="pop", verbose=FALSE) })

    # chromosome list
    chrlst <- unique(seqGetData(gdsfile, "chromosome"))
    if (length(chrlst) <= 0L) stop("No selected variant!")
    ans_tab <- ans_idx <- NULL
    for (chr in chrlst)
    {
        if (verbose)
            cat("Chromosome ", chr, ", ", sep="")
        seqSetFilterChrom(gdsfile, include=chr, intersect=TRUE, verbose=FALSE)
        idx <- which(seqGetFilter(gdsfile)$variant.sel)
        pos <- seqGetData(gdsfile, "position")
        if (!is.unsorted(pos) || pos[1L]>pos[length(pos)])
        {
            i <- order(pos)
            pos <- pos[i]; idx <- idx[i]
        }
        # generated by sliding windows
        v <- .Call(SEQ_Unit_SlidingWindows, pos, idx, win.size, win.shift, win.start,
            dup.rm, integer(length(pos)))
        names(v[[2L]]) <- rep(paste0("chr", chr), length(v[[2L]]))
        ans_idx <- c(ans_idx, v[[2L]])
        v <- v[[1L]]
        ans_tab <- rbind(ans_tab, data.frame(
            chr=rep(chr, length(v)), start=v, end=as.integer(v+win.size-1L),
            stringsAsFactors=FALSE))
        if (verbose)
            cat("# of units: ", length(v), "\n", sep="")
        # reset
        seqSetFilter(gdsfile, action="pop", verbose=FALSE)
        seqSetFilter(gdsfile, action="push", verbose=FALSE)
    }
    if (verbose)
        cat("# of units in total: ", length(ans_idx), "\n", sep="")

    # output
    ans <- list(desp=ans_tab, index=ans_idx)
    class(ans) <- "SeqUnitListClass"
    ans
}


#######################################################################
# Get a list of units of selected variants via sliding windows based on basepairs
#
seqUnitApply <- function(gdsfile, units, var.name, FUN,
    as.is=c("none", "list", "unlist"), parallel=FALSE, ...,
    .bl_size=256L, .progress=FALSE, .useraw=FALSE, .padNA=TRUE, .tolist=FALSE,
    .envir=NULL)
{
    # check
    stopifnot(inherits(gdsfile, "SeqVarGDSClass"))
    stopifnot(inherits(units, "SeqUnitListClass"))
    stopifnot(is.character(var.name), length(var.name)>0L)
    FUN <- match.fun(FUN)
    stopifnot(length(units) > 0L)
    as.is <- match.arg(as.is)
    stopifnot(is.numeric(.bl_size), length(.bl_size)==1L, .bl_size>0L)
    stopifnot(is.logical(.progress), length(.progress)==1L)
    stopifnot(is.logical(.useraw), length(.useraw)==1L)
    stopifnot(is.logical(.padNA), length(.padNA)==1L)
    stopifnot(is.null(.envir) || is.environment(.envir) || is.list(.envir))

    # further check units
    stopifnot(is.data.frame(units$desp))
    stopifnot(is.list(units$index))
    stopifnot(nrow(units$desp) == length(units$index))
    stopifnot(all(sapply(units$index, is.integer)))

    # initialize internally
    .clear_varmap(gdsfile)
    .Call(SEQ_IntAssign, process_index, 1L)
    .Call(SEQ_IntAssign, process_count, 1L)

    # get the number of workers
    njobs <- .NumParallel(parallel)
    if (njobs == 1L)
    {
        # save state
        seqSetFilter(gdsfile, action="push", verbose=FALSE)
        on.exit({ seqSetFilter(gdsfile, action="pop", verbose=FALSE) })
        # progress information
        nl <- length(units$index)
        progress <- if (.progress) .seqProgress(nl, njobs) else NULL
        # for-loop
        ans <- vector("list", nl)
        for (i in seq_len(nl))
        {
            seqSetFilter(gdsfile, variant.sel=units$index[[i]], verbose=FALSE)
            x <- seqGetData(gdsfile, var.name, .useraw, .padNA, .tolist, .envir)
            ans[[i]] <- FUN(x, ...)
            .seqProgForward(progress, 1L)
        }
        # finalize
        remove(progress)

    } else {

        # parameters for load balancing
        nl <- length(units$index)
        .bl_size <- as.integer(.bl_size)
        if (.bl_size * njobs > nl)
        {
            .bl_size <- nl %/% njobs
            if (.bl_size <= 0L) .bl_size <- 1L
        }
        totnum <- nl %/% .bl_size
        if (nl %% .bl_size) totnum <- totnum + 1L

        # multiple processes
        if (.IsForking(parallel))
        {
            # forking
            .packageEnv$gdsfile <- gdsfile
            .packageEnv$units <- units$index
            .packageEnv$var.name <- var.name
            .packageEnv$envir <- .envir
            parallel <- parallel::makeForkCluster(njobs)
            on.exit({
                with(.packageEnv, gdsfile <- units <- var.name <- envir <- NULL)
                stopCluster(parallel)
            })
        } else {
            need_cluster <- is.numeric(parallel) || is.logical(parallel)
            if (need_cluster)
            {
                # no forking on windows
                parallel <- makeCluster(njobs)
            }
            # distribute the parameters to each node
            clusterCall(parallel, function(fn, ut, vn, env) {
                .packageEnv$gdsfile <- SeqArray::seqOpen(fn, allow.duplicate=TRUE)
                .packageEnv$units <- ut
                .packageEnv$var.name <- vn
                .packageEnv$envir <- env
            }, fn=gdsfile$filename, ut=units$index, vn=var.name, env=.envir)
            # finalize
            on.exit({
                clusterCall(parallel, function() {
                    SeqArray::seqClose(.packageEnv$gdsfile)
                    with(.packageEnv, gdsfile <- units <- var.name <- envir <- NULL)
                })
            })
            if (need_cluster)
                on.exit(stopCluster(parallel), add=TRUE)
        }
        # initialize internally
        clusterApply(parallel, 1:njobs, function(i, njobs) {
            .Call(SEQ_IntAssign, process_index, i)
            .Call(SEQ_IntAssign, process_count, njobs)
        }, njobs=njobs)

        # progress information
        progress <- if (.progress) .seqProgress(length(units$index), njobs) else NULL
        # distributed for-loop
        ans <- .DynamicClusterCall(parallel, totnum,
            .fun = function(i, FUN, .useraw, .bl_size, ...)
        {
            # chuck size
            n <- .bl_size
            k <- (i - 1L) * n
            if (k + n > length(.packageEnv$units))
                n <- length(.packageEnv$units) - k
            # temporary
            f <- .packageEnv$gdsfile
            vn <- .packageEnv$var.name
            env <- .packageEnv$envir
            rv <- vector("list", n)
            # set variant filter for each sub unit
            for (j in seq_len(n))
            {
                seqSetFilter(f, variant.sel=.packageEnv$units[[j+k]], verbose=FALSE)
                x <- seqGetData(f, vn, .useraw, .padNA, .tolist, env)
                rv[[j]] <- FUN(x, ...)
            }
            # return
            rv
        }, .combinefun="list",
            .updatefun=function(i) .seqProgForward(progress, .bl_size),
            FUN=FUN, .useraw=.useraw, .bl_size=.bl_size, ...)
        ans <- unlist(ans, recursive=FALSE)
        # finalize
        remove(progress)
    }

    # output
    if (as.is == "unlist")
        ans <- unlist(ans, recursive=FALSE)
    else if (as.is == "none")
        ans <- invisible()
    ans
}
